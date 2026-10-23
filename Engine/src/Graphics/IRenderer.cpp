// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/IRenderer.hpp"
#include "FemboyEngine/Accessibility.hpp"
#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Simd.hpp"
#include "FemboyEngine/TextLayout.hpp"
#include <algorithm>
#include <cmath>

namespace Fem
{

namespace
{
sf::Vector2f RotateAround(sf::Vector2f p, sf::Vector2f center, float radians)
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float dx = p.x - center.x;
    const float dy = p.y - center.y;
    return sf::Vector2f(center.x + dx * c - dy * s, center.y + dx * s + dy * c);
}

std::uint32_t TextStyleBits(const TextStyle& style)
{
    std::uint32_t bits = sf::Text::Regular;
    if (style.bold)
    {
        bits |= sf::Text::Bold;
    }
    if (style.italic)
    {
        bits |= sf::Text::Italic;
    }
    if (style.underline)
    {
        bits |= sf::Text::Underlined;
    }
    if (style.strike)
    {
        bits |= sf::Text::StrikeThrough;
    }
    return bits;
}

float RunAdvance(const sf::Font& font, const sf::String& text, unsigned size, bool bold)
{
    // Summing advances rather than using getLocalBounds() keeps consecutive
    // styled runs from overlapping: local bounds is ink extents, not the pen
    // position after the last glyph.
    float advance = 0.f;
    for (const char32_t codePoint : text)
    {
        advance += font.getGlyph(codePoint, size, bold).advance;
    }
    return advance;
}
} // namespace

SFMLRenderer::SFMLRenderer() = default;

SFMLRenderer::~SFMLRenderer()
{
    Shutdown();
}

sf::RenderWindow* SFMLRenderer::ActiveWindow()
{
    return externalWindow_ ? externalWindow_ : ownedWindow_.get();
}

const sf::RenderWindow* SFMLRenderer::ActiveWindow() const
{
    return externalWindow_ ? externalWindow_ : ownedWindow_.get();
}

void SFMLRenderer::SetExternalWindow(sf::RenderWindow* window)
{
    externalWindow_ = window;
}

bool SFMLRenderer::Initialize(int w, int h, const std::string& title)
{
    BOY_INFO("IRenderer", "initializing SFML backend: " + title);
    width_ = w;
    height_ = h;

    if (!externalWindow_)
    {
        try
        {
            ownedWindow_ = std::make_unique<sf::RenderWindow>(sf::VideoMode({(unsigned)w, (unsigned)h}), title);
        }
        catch (const std::exception& e)
        {
            BOY_ERROR("IRenderer", std::string("window creation threw: ") + e.what());
            ownedWindow_.reset();
            return false;
        }
        if (!ownedWindow_->isOpen())
        {
            BOY_ERROR("IRenderer", "failed to create window");
            ownedWindow_.reset();
            return false;
        }
        ownedWindow_->setFramerateLimit(60);
    }

    camera_.setSize({(float)w, (float)h});
    camera_.setCenter({w / 2.f, h / 2.f});
    if (sf::RenderWindow* wnd = ActiveWindow())
    {
        wnd->setView(camera_);
    }
    BOY_INFO("IRenderer", "SFML backend initialized");
    return true;
}

void SFMLRenderer::Shutdown()
{
    // Never close a window we do not own: the engine keeps ownership of the
    // shared window and closes it during its own shutdown.
    if (ownedWindow_)
    {
        ownedWindow_->close();
        ownedWindow_.reset();
    }
    externalWindow_ = nullptr;
    FrameDrawList_.Clear();
}

bool SFMLRenderer::EnsureFxaa()
{
    if (fxaaTried_)
    {
        return fxaaReady_;
    }

    fxaaTried_ = true;
    static const char* vertex = R"(
void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;
})";

    static const char* fragment = R"(
uniform sampler2D texture;
uniform vec2 resolution;

float luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main()
{
    // The source is a render texture, which SFML stores bottom-up. A custom
    // fragment shader has to invert v itself; the default sprite path does it
    // through the texture matrix.
    vec2 uv = vec2(gl_TexCoord[0].x, 1.0 - gl_TexCoord[0].y);
    vec2 rcp = 1.0 / resolution;

    vec3 rgbNW = texture2D(texture, uv + vec2(-1.0, -1.0) * rcp).rgb;
    vec3 rgbNE = texture2D(texture, uv + vec2( 1.0, -1.0) * rcp).rgb;
    vec3 rgbSW = texture2D(texture, uv + vec2(-1.0,  1.0) * rcp).rgb;
    vec3 rgbSE = texture2D(texture, uv + vec2( 1.0,  1.0) * rcp).rgb;
    vec3 rgbM  = texture2D(texture, uv).rgb;

    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);
    float lumaM  = luma(rgbM);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    if (lumaMax - lumaMin < max(0.0312, lumaMax * 0.125))
    {
        gl_FragColor = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * 0.03125, 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * rcp;

    vec3 rgbA = 0.5 * (texture2D(texture, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture2D(texture, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture2D(texture, uv + dir * -0.5).rgb +
                                     texture2D(texture, uv + dir * 0.5).rgb);

    float lumaB = luma(rgbB);
    vec3 result = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    gl_FragColor = vec4(result, 1.0);
})";

    fxaaReady_ = fxaaShader_.loadFromMemory(vertex, fragment);
    if (!fxaaReady_)
    {
        BOY_WARN("IRenderer", "FXAA shader failed to compile; anti-aliasing falls back to off");
    }
    return fxaaReady_;
}

bool SFMLRenderer::EnsurePostShader()
{
    if (postTried_)
    {
        return postReady_;
    }

    postTried_ = true;
    static const char* vertex = R"(
void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;
})";

    static const char* fragment = R"(
uniform sampler2D texture;
uniform vec2 resolution;
uniform float gamma;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float eyeComfort;
uniform float eyeRed;
uniform float vignette;
uniform float scanlines;
uniform float aberration;
uniform float noise;
uniform float noiseTime;

float hash21(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    // Render targets in SFML are stored bottom-up; the default sprite blit
    // compensates for that automatically, but a raw gl_TexCoord lookup does not.
    vec2 uv = vec2(gl_TexCoord[0].x, 1.0 - gl_TexCoord[0].y);
    vec2 centered = uv - vec2(0.5);

    // Chromatic aberration is a tiny radial R/B split. At zero it is exactly
    // the original sample, so a default profile stays pixel-identical.
    vec3 color;
    if (aberration > 0.0001)
    {
        vec2 offset = centered * aberration * 0.006;
        color.r = texture2D(texture, uv + offset).r;
        color.g = texture2D(texture, uv).g;
        color.b = texture2D(texture, uv - offset).b;
    }
    else
    {
        color = texture2D(texture, uv).rgb;
    }

    // Brightness/contrast around mid-grey, then gamma. Defaults (1,1,1) leave
    // the frame untouched.
    color = (color - vec3(0.5)) * contrast + vec3(0.5);
    color *= brightness;
    color = pow(max(color, vec3(0.0)), vec3(1.0 / max(gamma, 0.01)));

    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, saturation);

    // Eye comfort: a warm yellow or a deeper red filter. This is game-side, not
    // an OS colour-temperature change.
    vec3 warm = vec3(1.0, 0.88, 0.65);
    vec3 red  = vec3(1.0, 0.66, 0.52);
    vec3 tint = mix(warm, red, clamp(eyeRed, 0.0, 1.0));
    color *= mix(vec3(1.0), tint, clamp(eyeComfort, 0.0, 1.0));

    if (vignette > 0.0001)
    {
        float d = smoothstep(0.30, 0.82, length(centered) * 1.55);
        color *= 1.0 - vignette * d;
    }

    if (scanlines > 0.0001)
    {
        float line = 0.5 + 0.5 * sin(uv.y * max(resolution.y, 1.0) * 3.14159265);
        color *= 1.0 - scanlines * 0.16 * (1.0 - line);
    }

    // A tiny animated grain. Unlike a static overlay it changes which subpixels
    // are lit, which is exactly what breaks up a long-lived flat HUD region on
    // an OLED panel. The default is off; 0.01..0.03 is nearly invisible.
    if (noise > 0.0001)
    {
        float n = hash21(gl_FragCoord.xy + vec2(noiseTime * 91.7, noiseTime * 37.3));
        color += (n - 0.5) * noise;
    }

    gl_FragColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
})";

    postReady_ = postShader_.loadFromMemory(vertex, fragment);
    if (!postReady_)
    {
        BOY_WARN("IRenderer", "post-process shader failed to compile; display effects fall back to off");
    }
    return postReady_;
}

void SFMLRenderer::SetAntiAliasing(AntiAliasingMode mode)
{
    if (antiAliasingMode_ == mode)
    {
        return;
    }
    antiAliasingMode_ = mode;
    if (mode != AntiAliasingMode::Fxaa)
    {
        fxaaReady_ = false;
    }
    fxaaTried_ = false;
}

void SFMLRenderer::SetPostProcess(const PostProcessParams& params)
{
    postParams_ = params;
}

void SFMLRenderer::SetUiPixelShift(sf::Vector2i shift)
{
    uiPixelShift_ = shift;
}

void SFMLRenderer::SetUiDissolve(float amount, std::uint32_t seed)
{
    uiDissolve_ = std::clamp(amount, 0.f, 1.f);
    uiDissolveSeed_ = seed;
}

void SFMLRenderer::BeginFrame()
{
    FrameDrawList_.Clear();
    stats_ = {};
    postTime_ += postClock_.restart().asSeconds();
    sf::RenderWindow* w = ActiveWindow();
    if (!w)
    {
        return;
    }

    // The window may have been resized since initialize(); width()/height()
    // are used by editors and UI code, so keep them in sync.
    width_ = static_cast<int>(w->getSize().x);
    height_ = static_cast<int>(w->getSize().y);

    postActive_ = postParams_.Active() && EnsurePostShader();
    scaled_ = (renderScale_ != 1.f || (antiAliasingMode_ == AntiAliasingMode::Fxaa && EnsureFxaa()) || postActive_) &&
              width_ > 0 && height_ > 0;
    if (scaled_)
    {
        // The driver's texture limit (same limit a render target is subject to).
        const unsigned maxSize = sf::Texture::getMaximumSize();
        auto scaledWidth = static_cast<unsigned>(std::lround(static_cast<float>(width_) * renderScale_));
        auto scaledHeight = static_cast<unsigned>(std::lround(static_cast<float>(height_) * renderScale_));
        // A 200% scale on a 4K display exceeds some GPUs' maximum texture size;
        // clamping keeps the frame instead of losing the whole render target.
        scaledWidth = std::clamp(scaledWidth, 1u, maxSize);
        scaledHeight = std::clamp(scaledHeight, 1u, maxSize);
        const sf::Vector2u size{scaledWidth, scaledHeight};
        if (renderTexture_.getSize() != size)
        {
            if (!renderTexture_.resize(size))
            {
                BOY_ERROR("IRenderer", "could not resize the offscreen target to " + std::to_string(size.x) + "x" +
                                           std::to_string(size.y) + "; falling back to native resolution");
                scaled_ = false;
                postActive_ = false;
            }
        }
    }

    if (scaled_)
    {
        renderTexture_.clear();
        renderTexture_.setView(camera_);
    }
    else
    {
        w->clear();
        w->setView(camera_);
    }
}

void SFMLRenderer::EndFrame()
{
    sf::RenderWindow* w = ActiveWindow();
    if (!w)
    {
        FrameDrawList_.Clear();
        return;
    }

    // Everything below draws to `target`: the offscreen target when the render
    // scale is not 1.0, the window otherwise. Only the final blit touches the
    // window, so supersampling costs one extra full-screen quad.
    sf::RenderTarget* target =
        scaled_ ? static_cast<sf::RenderTarget*>(&renderTexture_) : static_cast<sf::RenderTarget*>(w);

    // One sort for the whole frame: sprites, text, shapes and pre-built geometry
    // all land at their (layer, z) depth instead of in submission order.
    FrameDrawList_.Sort();
    stats_.commands = FrameDrawList_.Commands().size();

    sf::VertexArray batch;
    const sf::Texture* curTexPtr = nullptr;
    sf::BlendMode batchBlend = sf::BlendAlpha;
    bool batchCustomBlend = false;
    bool worldSpace = true;

    auto flush = [&]()
    {
        if (batch.getVertexCount() == 0)
        {
            return;
        }
        sf::RenderStates rs;
        rs.texture = curTexPtr;
        rs.blendMode = batchBlend;
        if (worldSpace)
        {
            rs.transform = worldTransform_;
        }
        target->draw(batch, rs);
        ++stats_.drawCalls;
        stats_.vertices += batch.getVertexCount();
        if (curTexPtr)
        {
            ++stats_.textureBinds;
        }
        batch.clear();
    };
    auto flushAll = [&]()
    {
        flush();
        curTexPtr = nullptr;
    };

    // Shapes no longer become one draw call each. Rectangles, circles and lines
    // append triangles to the same vertex array as sprites; a texture change is
    // the only batch boundary. The primitive type is always Triangles, so a
    // shape batch and a sprite batch can share the flush path.
    auto appendTriangle = [&](sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Color color)
    {
        batch.append(sf::Vertex{a, color});
        batch.append(sf::Vertex{b, color});
        batch.append(sf::Vertex{c, color});
    };
    auto appendQuad = [&](sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d, sf::Color color)
    {
        appendTriangle(a, b, c, color);
        appendTriangle(a, c, d, color);
    };
    auto beginShapeBatch = [&]()
    {
        if (curTexPtr && batch.getVertexCount() > 0)
        {
            flushAll();
        }
        if (batch.getVertexCount() == 0)
        {
            batch.setPrimitiveType(sf::PrimitiveType::Triangles);
            curTexPtr = nullptr;
        }
    };

    // UI/overlay/debug commands are authored in window pixels, not world units.
    // Commands are sorted by layer, so the whole frame crosses this boundary at
    // most once.
    //
    // The screen view is rebuilt from the size we synced in beginFrame() instead
    // of using getDefaultView(): SFML's default view can lag behind a resize
    // (it is refreshed by the resize event, which a project that polls events
    // itself may consume), and any mismatch makes UI authored in `width()` /
    // `height()` coordinates drift off screen as the window grows.
    const sf::View screenView(sf::FloatRect({0.f, 0.f}, {static_cast<float>(width_), static_cast<float>(height_)}));
    bool screenSpace = false;
    auto useView = [&](bool screen)
    {
        if (screen == screenSpace)
        {
            return;
        }
        flushAll();
        target->setView(screen ? screenView : camera_);
        screenSpace = screen;
        worldSpace = !screen;
    };

    // Scissor state. SFML applies it through the view, so a clipped command
    // flushes the batch, swaps in a scissored screen view, and the next
    // unclipped command swaps it back.
    bool clipActive = false;
    sf::FloatRect activeClip;
    auto sameClip = [](const sf::FloatRect& a, const sf::FloatRect& b)
    {
        return a.position.x == b.position.x && a.position.y == b.position.y && a.size.x == b.size.x &&
               a.size.y == b.size.y;
    };
    auto applyClip = [&](const DrawCommand& c)
    {
        if (c.hasClip && c.clip.size.x > 0.f && c.clip.size.y > 0.f && width_ > 0 && height_ > 0)
        {
            // Skirt attaches the active clip to every command. Re-applying the
            // same scissor used to flush the batch for every single rectangle,
            // line and circle inside a scrolled list or the skill tree, turning
            // a handful of draws into hundreds. Only a real clip change is a
            // batch boundary.
            if (clipActive && sameClip(activeClip, c.clip))
            {
                return;
            }
            flushAll();
            sf::View clipped = screenView;
            clipped.setScissor(sf::FloatRect(
                {c.clip.position.x / static_cast<float>(width_), c.clip.position.y / static_cast<float>(height_)},
                {c.clip.size.x / static_cast<float>(width_), c.clip.size.y / static_cast<float>(height_)}));
            target->setView(clipped);
            clipActive = true;
            activeClip = c.clip;
        }
        else if (clipActive)
        {
            flushAll();
            target->setView(screenSpace ? screenView : camera_);
            clipActive = false;
            activeClip = {};
        }
    };

    // Conservative world-space culling volume. The camera rectangle is in
    // projected view space; inverse-project its corners before building the
    // world AABB so an isometric transform does not cull visible sprites.
    const sf::Vector2f center = camera_.getCenter();
    const sf::Vector2f size = camera_.getSize();
    const float halfDiag = std::sqrt(size.x * size.x + size.y * size.y) * 0.5f;
    const sf::Transform inverseWorld = worldTransform_.getInverse();
    const sf::Vector2f viewCorners[4] = {{center.x - halfDiag, center.y - halfDiag},
                                         {center.x + halfDiag, center.y - halfDiag},
                                         {center.x + halfDiag, center.y + halfDiag},
                                         {center.x - halfDiag, center.y + halfDiag}};
    sf::Vector2f minWorld = inverseWorld.transformPoint(viewCorners[0]);
    sf::Vector2f maxWorld = minWorld;
    for (int i = 1; i < 4; ++i)
    {
        const sf::Vector2f p = inverseWorld.transformPoint(viewCorners[i]);
        minWorld = {std::min(minWorld.x, p.x), std::min(minWorld.y, p.y)};
        maxWorld = {std::max(maxWorld.x, p.x), std::max(maxWorld.y, p.y)};
    }
    const sf::FloatRect worldBounds(minWorld, {maxWorld.x - minWorld.x, maxWorld.y - minWorld.y});
    auto visible = [&](const sf::FloatRect& bounds)
    {
        return bounds.position.x < worldBounds.position.x + worldBounds.size.x &&
               bounds.position.x + bounds.size.x > worldBounds.position.x &&
               bounds.position.y < worldBounds.position.y + worldBounds.size.y &&
               bounds.position.y + bounds.size.y > worldBounds.position.y;
    };

    Accessibility& access = Accessibility::Instance();
    const bool adjustColors = access.GetColorblindMode() != ColorblindMode::None || access.HighContrast();
    const bool reduceFlash = access.ReduceFlash();

    for (std::size_t commandIndex = 0; commandIndex < FrameDrawList_.Commands().size(); ++commandIndex)
    {
        const DrawCommand& source = FrameDrawList_.Commands()[commandIndex];
        const bool screenLayer = source.layer >= DrawLayer::UI;

        // OLED pixel shift: only the screen-space UI/overlay layers move. A
        // full-screen dim/backdrop is kept in place so shifting never leaves a
        // one-pixel seam at the window edge.
        bool fullScreen = false;
        if (source.kind == DrawKind::Rectangle)
        {
            fullScreen = source.position.x <= 0.5f && source.position.y <= 0.5f &&
                         source.size.x >= static_cast<float>(width_) - 0.5f &&
                         source.size.y >= static_cast<float>(height_) - 0.5f;
        }
        const bool wantShift = screenLayer && !fullScreen && (uiPixelShift_.x != 0 || uiPixelShift_.y != 0);
        // Dissolve does apply to a full-screen modal backdrop: fading the dim
        // layer with the panel reveals the moving game world underneath instead
        // of leaving a large static rectangle lit on an OLED panel.
        const bool wantDissolve = screenLayer && uiDissolve_ > 0.0001f;

        DrawCommand adjusted;
        const DrawCommand* command = &source;
        if (wantShift || wantDissolve || adjustColors || reduceFlash)
        {
            adjusted = source;
            if (wantShift)
            {
                const sf::Vector2f offset(static_cast<float>(uiPixelShift_.x), static_cast<float>(uiPixelShift_.y));
                adjusted.position += offset;
                if (adjusted.kind == DrawKind::Line)
                {
                    adjusted.size += offset;
                }
                if (adjusted.hasClip)
                {
                    adjusted.clip.position += offset;
                }
                if (adjusted.kind == DrawKind::Vertices)
                {
                    for (std::size_t i = 0; i < adjusted.vertices.getVertexCount(); ++i)
                    {
                        adjusted.vertices[i].position += offset;
                    }
                }
            }
            if (wantDissolve)
            {
                // Stable per-command threshold: each widget starts fading at a
                // different point, but the order does not shimmer frame to frame.
                std::uint32_t hash =
                    static_cast<std::uint32_t>(commandIndex) * 747796405u + uiDissolveSeed_ * 2891336453u;
                hash ^= hash >> 16;
                hash *= 2246822519u;
                hash ^= hash >> 13;
                hash *= 3266489917u;
                hash ^= hash >> 16;
                const float order = static_cast<float>(hash & 0xFFFFu) / 65535.f;
                const float local = std::clamp((uiDissolve_ - order * 0.35f) / 0.65f, 0.f, 1.f);
                const float alpha = 1.f - local;
                const auto fade = [alpha](const sf::Color& color)
                {
                    sf::Color result = color;
                    result.a = static_cast<std::uint8_t>(
                        std::lround(static_cast<float>(color.a) * std::clamp(alpha, 0.f, 1.f)));
                    return result;
                };
                adjusted.color = fade(adjusted.color);
                adjusted.outline = fade(adjusted.outline);
                adjusted.shadow = fade(adjusted.shadow);
                if (adjusted.kind == DrawKind::Text)
                {
                    for (TextRun& run : adjusted.textRuns)
                    {
                        run.style.color = fade(run.style.color);
                    }
                }
                if (adjusted.kind == DrawKind::Vertices)
                {
                    for (std::size_t i = 0; i < adjusted.vertices.getVertexCount(); ++i)
                    {
                        adjusted.vertices[i].color = fade(adjusted.vertices[i].color);
                    }
                }
            }
            if (adjustColors)
            {
                adjusted.color = access.AdjustColor(adjusted.color);
                adjusted.outline = access.AdjustColor(adjusted.outline);
                adjusted.shadow = access.AdjustColor(adjusted.shadow);
                if (adjusted.kind == DrawKind::Text)
                {
                    for (TextRun& run : adjusted.textRuns)
                    {
                        run.style.color = access.AdjustColor(run.style.color);
                    }
                }
                if (adjusted.kind == DrawKind::Vertices)
                {
                    for (std::size_t i = 0; i < adjusted.vertices.getVertexCount(); ++i)
                    {
                        adjusted.vertices[i].color = access.AdjustColor(adjusted.vertices[i].color);
                    }
                }
            }
            if (reduceFlash && adjusted.kind == DrawKind::Rectangle)
            {
                // Photosensitivity guard. It only touches near-full-screen colour
                // rectangles, which is what an unguarded flash/modal backdrop
                // usually is; a normal HUD panel is much smaller than this.
                const bool almostFullScreen =
                    adjusted.size.x >= static_cast<float>(width_) * 0.8f &&
                    adjusted.size.y >= static_cast<float>(height_) * 0.8f;
                if (almostFullScreen)
                {
                    adjusted.color.a = std::min<std::uint8_t>(adjusted.color.a, 96);
                }
            }
            command = &adjusted;
        }
        const DrawCommand& c = *command;
        useView(screenLayer);
        applyClip(c);

        // A custom blend (the lightmap's multiply, an additive glow, ...) is a
        // batch boundary. Commands without one return to alpha blending, which
        // is the common case and stays batched.
        if (c.customBlend)
        {
            if (!batchCustomBlend)
            {
                flushAll();
                batchCustomBlend = true;
                batchBlend = c.blend;
            }
        }
        else if (batchCustomBlend)
        {
            flushAll();
            batchCustomBlend = false;
            batchBlend = sf::BlendAlpha;
        }

        switch (c.kind)
        {
            case DrawKind::Text:
            {
                flushAll();
                sf::RenderStates textStates;
                if (worldSpace)
                {
                    textStates.transform = worldTransform_;
                }
                if (!c.textRuns.empty())
                {
                    struct Prepared
                    {
                        sf::Text text;
                        float advance = 0.f;
                    };
                    std::vector<Prepared> prepared;
                    prepared.reserve(c.textRuns.size());
                    float total = 0.f;

                    for (const TextRun& run : c.textRuns)
                    {
                        const std::string display = Text::PrepareForDisplay(run.text);
                        const sf::String string = Text::FromUtf8(display);
                        const std::string fontId = run.style.fontId.empty() ? c.fontId : run.style.fontId;
                        sf::Font& font = AssetManager::Instance().FontForText(fontId, string);
                        const unsigned size = run.style.size > 0 ? run.style.size : c.textSize;

                        sf::Text draw(font, string, size);
                        draw.setFillColor(run.style.color);
                        draw.setStyle(TextStyleBits(run.style));
                        if (c.outline.a > 0)
                        {
                            draw.setOutlineColor(c.outline);
                            draw.setOutlineThickness(c.thickness > 0.f ? c.thickness : 1.f);
                        }

                        const float advance = RunAdvance(font, string, size, run.style.bold);
                        total += advance;
                        prepared.push_back(Prepared{std::move(draw), advance});
                    }

                    float x = c.position.x;
                    if (c.textAlign == TextAlign::Center)
                    {
                        x -= total * 0.5f;
                    }
                    else if (c.textAlign == TextAlign::Right)
                    {
                        x -= total;
                    }

                    for (Prepared& entry : prepared)
                    {
                        entry.text.setPosition({x, c.position.y});
                        if (c.shadow.a > 0)
                        {
                            sf::Text shadow = entry.text;
                            shadow.setFillColor(c.shadow);
                            shadow.setOutlineThickness(0.f);
                            shadow.setPosition({x + c.shadowOffset.x, c.position.y + c.shadowOffset.y});
                            target->draw(shadow, textStates);
                            ++stats_.drawCalls;
                        }
                        target->draw(entry.text, textStates);
                        ++stats_.drawCalls;
                        x += entry.advance;
                    }
                    break;
                }

                const std::string display = Text::PrepareForDisplay(c.text);
                const sf::String string = Text::FromUtf8(display);
                sf::Font& textFont = AssetManager::Instance().FontForText(c.fontId, string);
                sf::Text t(textFont, string, c.textSize);
                t.setPosition(c.position);
                t.setFillColor(c.color);
                if (c.outline.a > 0)
                {
                    t.setOutlineColor(c.outline);
                    t.setOutlineThickness(c.thickness > 0.f ? c.thickness : 1.f);
                }
                if (c.shadow.a > 0)
                {
                    sf::Text shadow = t;
                    shadow.setFillColor(c.shadow);
                    shadow.setOutlineThickness(0.f);
                    shadow.setPosition({c.position.x + c.shadowOffset.x, c.position.y + c.shadowOffset.y});
                    target->draw(shadow, textStates);
                    ++stats_.drawCalls;
                }
                target->draw(t, textStates);
                ++stats_.drawCalls;
                break;
            }
            case DrawKind::Rectangle:
            {
                if (!screenLayer && !visible({c.position, c.size}))
                {
                    break;
                }
                const float w = std::max(0.f, c.size.x);
                const float h = std::max(0.f, c.size.y);
                if (w <= 0.f || h <= 0.f)
                {
                    break;
                }

                beginShapeBatch();
                const float x = c.position.x;
                const float y = c.position.y;
                appendQuad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, c.color);

                if (c.outline.a > 0)
                {
                    const float t = std::min(c.thickness > 0.f ? c.thickness : 1.f, std::min(w, h) * 0.5f);
                    if (t > 0.f)
                    {
                        // Four inside strips. Keeping the outline in the same batch
                        // matters for HUD panels: a border used to double draw calls.
                        appendQuad({x, y}, {x + w, y}, {x + w, y + t}, {x, y + t}, c.outline);
                        appendQuad({x, y + h - t}, {x + w, y + h - t}, {x + w, y + h}, {x, y + h}, c.outline);
                        appendQuad({x, y + t}, {x + t, y + t}, {x + t, y + h - t}, {x, y + h - t}, c.outline);
                        appendQuad({x + w - t, y + t}, {x + w, y + t}, {x + w, y + h - t}, {x + w - t, y + h - t},
                                   c.outline);
                    }
                }
                break;
            }
            case DrawKind::Circle:
            {
                const float radius = std::max(0.f, c.radius);
                if (radius <= 0.f || (!screenLayer && !visible({{c.position.x - radius, c.position.y - radius},
                                                                {radius * 2.f, radius * 2.f}})))
                {
                    break;
                }

                beginShapeBatch();
                constexpr int segments = 24;
                constexpr float tau = 6.28318530717958647692f;
                for (int i = 0; i < segments; ++i)
                {
                    const float a0 = tau * static_cast<float>(i) / static_cast<float>(segments);
                    const float a1 = tau * static_cast<float>(i + 1) / static_cast<float>(segments);
                    appendTriangle(
                        c.position, {c.position.x + std::cos(a0) * radius, c.position.y + std::sin(a0) * radius},
                        {c.position.x + std::cos(a1) * radius, c.position.y + std::sin(a1) * radius}, c.color);
                }

                if (c.outline.a > 0)
                {
                    const float inner = radius - (c.thickness > 0.f ? c.thickness : 1.f);
                    if (inner > 0.f)
                    {
                        for (int i = 0; i < segments; ++i)
                        {
                            const float a0 = tau * static_cast<float>(i) / static_cast<float>(segments);
                            const float a1 = tau * static_cast<float>(i + 1) / static_cast<float>(segments);
                            appendQuad({c.position.x + std::cos(a0) * radius, c.position.y + std::sin(a0) * radius},
                                       {c.position.x + std::cos(a1) * radius, c.position.y + std::sin(a1) * radius},
                                       {c.position.x + std::cos(a1) * inner, c.position.y + std::sin(a1) * inner},
                                       {c.position.x + std::cos(a0) * inner, c.position.y + std::sin(a0) * inner},
                                       c.outline);
                        }
                    }
                }
                break;
            }
            case DrawKind::Line:
            {
                const sf::Vector2f a = c.position;
                const sf::Vector2f b = c.size; // second endpoint for lines
                const float width = c.thickness > 0.f ? c.thickness : 1.f;
                sf::Vector2f dir = b - a;
                const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len <= 0.00001f)
                {
                    break;
                }
                dir /= len;
                const sf::Vector2f normal(-dir.y * width * 0.5f, dir.x * width * 0.5f);
                beginShapeBatch();
                appendQuad(a + normal, b + normal, b - normal, a - normal, c.color);
                break;
            }
            case DrawKind::Vertices:
            {
                if (c.vertices.getVertexCount() == 0)
                {
                    break;
                }
                flushAll();
                sf::RenderStates rs;
                const sf::Texture* texture = c.texture;
                if (!texture && !c.textureId.empty())
                {
                    texture = &AssetManager::Instance().Texture(c.textureId);
                }
                rs.texture = texture;
                rs.blendMode = c.customBlend ? c.blend : sf::BlendAlpha;
                if (worldSpace)
                {
                    rs.transform = worldTransform_;
                }
                target->draw(c.vertices, rs);
                ++stats_.drawCalls;
                stats_.vertices += c.vertices.getVertexCount();
                if (texture)
                {
                    ++stats_.textureBinds;
                }
                break;
            }
            case DrawKind::Sprite:
            {
                const sf::Texture& tex = c.texture ? *c.texture : AssetManager::Instance().Texture(c.textureId);
                const float texW = static_cast<float>(tex.getSize().x);
                const float texH = static_cast<float>(tex.getSize().y);

                // Resolve the source rectangle. A default-constructed (zero-sized)
                // source means the whole texture; a partially off-texture rect is
                // clamped instead of producing garbage texcoords.
                sf::FloatRect src = c.source;
                if (src.size.x <= 0.f || src.size.y <= 0.f)
                {
                    src = sf::FloatRect({0.f, 0.f}, {texW, texH});
                }
                src.position.x = std::clamp(src.position.x, 0.f, texW);
                src.position.y = std::clamp(src.position.y, 0.f, texH);
                src.size.x = std::clamp(src.size.x, 0.f, texW - src.position.x);
                src.size.y = std::clamp(src.size.y, 0.f, texH - src.position.y);
                if (src.size.x <= 0.f || src.size.y <= 0.f)
                {
                    break;
                }

                const float sw = src.size.x * c.scale.x;
                const float sh = src.size.y * c.scale.y;

                sf::Vector2f p0(c.position.x, c.position.y);
                sf::Vector2f p1(c.position.x + sw, c.position.y);
                sf::Vector2f p2(c.position.x + sw, c.position.y + sh);
                sf::Vector2f p3(c.position.x, c.position.y + sh);

                // Pivot inside the sprite, expressed in pixels. Defaulting to the
                // centre keeps the historical rotation behaviour.
                const sf::Vector2f pivot(c.position.x + sw * c.origin.x, c.position.y + sh * c.origin.y);

                if (c.rotation != 0.f)
                {
                    const float rad = c.rotation * 3.14159265358979323846f / 180.f;
                    sf::Vector2f corners[4] = {p0, p1, p2, p3};
                    Simd::RotatePoints4(corners, pivot, std::cos(rad), std::sin(rad));
                    p0 = corners[0];
                    p1 = corners[1];
                    p2 = corners[2];
                    p3 = corners[3];
                }

                if (!screenLayer)
                {
                    const float minX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
                    const float maxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
                    const float minY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
                    const float maxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
                    if (!visible({{minX, minY}, {maxX - minX, maxY - minY}}))
                    {
                        break;
                    }
                }

                const sf::Texture* texture = c.texture ? c.texture : &tex;
                // Compare the resolved pointer, not the string id. Repeated sprites
                // of one texture no longer pay a std::string compare per command,
                // and two ids that resolve to the same texture can share a batch.
                if (curTexPtr != texture && batch.getVertexCount() > 0)
                {
                    flushAll();
                }
                if (batch.getVertexCount() == 0)
                {
                    batch.setPrimitiveType(sf::PrimitiveType::Triangles);
                    curTexPtr = texture;
                }

                float u0 = src.position.x;
                float v0 = src.position.y;
                float u1 = src.position.x + src.size.x;
                float v1 = src.position.y + src.size.y;
                if (c.flipX)
                {
                    std::swap(u0, u1);
                }
                if (c.flipY)
                {
                    std::swap(v0, v1);
                }

                sf::Vertex v[4];
                v[0].position = p0;
                v[1].position = p1;
                v[2].position = p2;
                v[3].position = p3;
                v[0].texCoords = sf::Vector2f(u0, v0);
                v[1].texCoords = sf::Vector2f(u1, v0);
                v[2].texCoords = sf::Vector2f(u1, v1);
                v[3].texCoords = sf::Vector2f(u0, v1);
                for (int i = 0; i < 4; ++i)
                {
                    v[i].color = c.color;
                }

                batch.append(v[0]);
                batch.append(v[1]);
                batch.append(v[2]);
                batch.append(v[0]);
                batch.append(v[2]);
                batch.append(v[3]);
                break;
            }
        }
    }

    flushAll();

    if (clipActive)
    {
        target->setView(screenSpace ? screenView : camera_);
        clipActive = false;
    }

    if (scaled_)
    {
        // Blit the offscreen frame onto the window. The window view is the
        // whole window in pixels, so a scale of 1.5 downsamples smoothly.
        const sf::Vector2u source = renderTexture_.getSize();
        const sf::View screenView(sf::FloatRect({0.f, 0.f}, {static_cast<float>(width_), static_cast<float>(height_)}));
        w->setView(screenView);

        const auto preparePostUniforms = [&]()
        {
            postShader_.setUniform("resolution", sf::Vector2f(static_cast<float>(width_), static_cast<float>(height_)));
            postShader_.setUniform("gamma", postParams_.gamma);
            postShader_.setUniform("brightness", postParams_.brightness);
            postShader_.setUniform("contrast", postParams_.contrast);
            postShader_.setUniform("saturation", postParams_.saturation);
            postShader_.setUniform("eyeComfort", postParams_.eyeComfort);
            postShader_.setUniform("eyeRed", postParams_.eyeRed);
            postShader_.setUniform("vignette", postParams_.vignette);
            postShader_.setUniform("scanlines", postParams_.scanlines);
            postShader_.setUniform("aberration", postParams_.chromaticAberration);
            postShader_.setUniform("noise", postParams_.noise);
            postShader_.setUniform("noiseTime", postTime_);
        };

        bool fxaaPass = antiAliasingMode_ == AntiAliasingMode::Fxaa && EnsureFxaa();
        bool postPass = postActive_ && EnsurePostShader();

        // FXAA and colour grading both need a full-screen pass. When both are
        // enabled, FXAA lands in an intermediate window-sized target first, then
        // the post shader does the final blit. With only one of them the existing
        // single-blit path stays in use.
        if (fxaaPass && postPass)
        {
            const sf::Vector2u targetSize{static_cast<unsigned>(std::max(1, width_)),
                                          static_cast<unsigned>(std::max(1, height_))};
            bool ready = true;
            if (postTexture_.getSize() != targetSize)
            {
                ready = postTexture_.resize(targetSize);
            }
            if (!ready)
            {
                BOY_WARN("IRenderer", "could not allocate the FXAA/post intermediate target; skipping FXAA");
                fxaaPass = false;
            }
            else
            {
                postTexture_.clear();
                postTexture_.setView(screenView);
                sf::Sprite sourceSprite(renderTexture_.getTexture());
                sourceSprite.setScale({static_cast<float>(width_) / static_cast<float>(source.x),
                                       static_cast<float>(height_) / static_cast<float>(source.y)});
                fxaaShader_.setUniform("resolution",
                                       sf::Vector2f(static_cast<float>(source.x), static_cast<float>(source.y)));
                postTexture_.draw(sourceSprite, &fxaaShader_);
                ++stats_.drawCalls;
                stats_.vertices += 6;

                sf::Sprite postSprite(postTexture_.getTexture());
                preparePostUniforms();
                w->draw(postSprite, &postShader_);
                ++stats_.drawCalls;
                stats_.vertices += 6;
            }
        }

        if (!(fxaaPass && postPass))
        {
            sf::Sprite blit(renderTexture_.getTexture());
            blit.setScale({static_cast<float>(width_) / static_cast<float>(source.x),
                           static_cast<float>(height_) / static_cast<float>(source.y)});
            if (postPass)
            {
                preparePostUniforms();
                w->draw(blit, &postShader_);
            }
            else if (fxaaPass)
            {
                fxaaShader_.setUniform("resolution",
                                       sf::Vector2f(static_cast<float>(source.x), static_cast<float>(source.y)));
                w->draw(blit, &fxaaShader_);
            }
            else
            {
                w->draw(blit);
            }
            ++stats_.drawCalls;
            stats_.vertices += 6;
        }
    }

    // Leave the window on the world view for the next frame's beginFrame().
    w->setView(camera_);
    FrameDrawList_.Clear();
}

void SFMLRenderer::Clear(const sf::Color& c)
{
    if (scaled_)
    {
        renderTexture_.clear(c);
        return;
    }
    if (sf::RenderWindow* w = ActiveWindow())
    {
        w->clear(c);
    }
}

void SFMLRenderer::Present()
{
    if (sf::RenderWindow* w = ActiveWindow())
    {
        w->display();
    }
}

bool SFMLRenderer::IsOpen() const
{
    const sf::RenderWindow* w = ActiveWindow();
    return w && w->isOpen();
}

void SFMLRenderer::SetCamera(const sf::View& view)
{
    camera_ = view;
    if (scaled_)
    {
        renderTexture_.setView(view);
    }
    if (sf::RenderWindow* w = ActiveWindow())
    {
        w->setView(view);
    }
}

void SFMLRenderer::SetRenderScale(float scale)
{
    // 0.5x is a readable "performance" step; 2x is 4x the pixels, which is
    // already punishing at 1440p. Anything outside the range is a settings bug.
    renderScale_ = std::clamp(scale, 0.5f, 2.f);
}

float SFMLRenderer::RenderScale() const
{
    return renderScale_;
}

sf::View SFMLRenderer::Camera() const
{
    return camera_;
}

int SFMLRenderer::Width() const
{
    return width_;
}

int SFMLRenderer::Height() const
{
    return height_;
}

sf::RenderWindow* SFMLRenderer::GetWindow()
{
    return ActiveWindow();
}

} // namespace Fem
