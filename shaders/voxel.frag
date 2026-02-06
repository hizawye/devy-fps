#version 450 core

in vec4 vColor;
in vec3 vNormal;

out vec4 FragColor;

void main() {
  vec3 normal = normalize(vNormal);
  vec3 sun_dir = normalize(vec3(0.4, 1.0, 0.3));
  float diffuse = max(dot(normal, sun_dir), 0.0);
  float ambient = 0.35;
  vec3 lit = vColor.rgb * (ambient + diffuse * 0.65);
  FragColor = vec4(lit, vColor.a);
}
