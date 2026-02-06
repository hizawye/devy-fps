#pragma once

#include <glm/glm.hpp>
#include <string>

namespace devy::engine {

class Shader {
public:
  Shader() = default;
  ~Shader();

  bool load_from_files(const std::string& vertex_path, const std::string& fragment_path);
  void use() const;
  void set_mat4(const std::string& name, const glm::mat4& value) const;
  bool valid() const;

private:
  unsigned int program_ = 0;

  unsigned int compile_shader(unsigned int type, const std::string& source);
  std::string read_file(const std::string& path) const;
  void destroy();
};

} // namespace devy::engine
