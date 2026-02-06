#include "engine/Shader.h"

#include "shared/Log.h"

#include <glad/glad.h>

#include <fstream>
#include <sstream>

namespace devy::engine {

Shader::~Shader() {
  destroy();
}

void Shader::destroy() {
  if (program_ != 0) {
    glDeleteProgram(program_);
    program_ = 0;
  }
}

std::string Shader::read_file(const std::string& path) const {
  std::ifstream file(path);
  if (!file.is_open()) {
    devy::log::write(devy::log::Level::Error, "Failed to open shader: " + path);
    return {};
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

unsigned int Shader::compile_shader(unsigned int type, const std::string& source) {
  unsigned int shader = glCreateShader(type);
  const char* src = source.c_str();
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);

  int success = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info[1024];
    glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
    devy::log::write(devy::log::Level::Error, std::string("Shader compile error: ") + info);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

bool Shader::load_from_files(const std::string& vertex_path, const std::string& fragment_path) {
  destroy();
  auto vertex_source = read_file(vertex_path);
  auto fragment_source = read_file(fragment_path);
  if (vertex_source.empty() || fragment_source.empty()) {
    return false;
  }

  unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
  unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
  if (vertex_shader == 0 || fragment_shader == 0) {
    return false;
  }

  program_ = glCreateProgram();
  glAttachShader(program_, vertex_shader);
  glAttachShader(program_, fragment_shader);
  glLinkProgram(program_);

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  int success = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &success);
  if (!success) {
    char info[1024];
    glGetProgramInfoLog(program_, sizeof(info), nullptr, info);
    devy::log::write(devy::log::Level::Error, std::string("Shader link error: ") + info);
    destroy();
    return false;
  }

  return true;
}

void Shader::use() const {
  if (program_ != 0) {
    glUseProgram(program_);
  }
}

void Shader::set_mat4(const std::string& name, const glm::mat4& value) const {
  if (program_ == 0) {
    return;
  }
  int location = glGetUniformLocation(program_, name.c_str());
  if (location >= 0) {
    glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
  }
}

bool Shader::valid() const {
  return program_ != 0;
}

} // namespace devy::engine
