uniform sampler2D texture;
uniform float strength;

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 color = texture2D(texture, uv);
    float dist = distance(uv, vec2(0.5, 0.5));
    float vignette = smoothstep(0.8, 0.2, dist * strength);
    gl_FragColor = vec4(color.rgb * vignette, color.a);
}