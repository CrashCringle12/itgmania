#ifndef RAGE_SHADER_PROGRAM_H
#define RAGE_SHADER_PROGRAM_H

#include <map>
#include <memory>
#include <string>
#include <vector>

class RageTexture;
struct lua_State;

// CPU-side program state is retained across renderer/context recreation.
// GPU names belong exclusively to the renderer-specific implementation.
class RageShaderProgram
    : public std::enable_shared_from_this<RageShaderProgram> {
 public:
  struct Uniform {
    enum Kind { Float, Int, Matrix } kind = Float;
    int arity = 1;  // Vector width, or matrix dimension for Matrix.
    std::vector<float> floats;
    std::vector<int> integers;
  };

  RageShaderProgram();
  virtual ~RageShaderProgram();
  RageShaderProgram(const RageShaderProgram&) = delete;
  RageShaderProgram& operator=(const RageShaderProgram&) = delete;

  virtual bool Compile() = 0;
  virtual void Apply() = 0;
  virtual bool IsValid() const = 0;
  // Invalidate forgets names belonging to a context which no longer exists.
  virtual void Invalidate() = 0;
  // ReleaseGPUResources deletes names while their context is still current.
  virtual void ReleaseGPUResources() = 0;

  void SetSources(
      const std::string& vertexPath, const std::string& fragmentPath,
      const std::string& vertexSource, const std::string& fragmentSource);
  bool LoadSources(
      const std::string& vertexPath, const std::string& fragmentPath);
  const std::string& GetVertexSource() const { return m_vertexSource; }
  const std::string& GetFragmentSource() const { return m_fragmentSource; }
  const std::string& GetVertexPath() const { return m_vertexPath; }
  const std::string& GetFragmentPath() const { return m_fragmentPath; }
  const std::map<std::string, std::string>& GetDefines() const {
    return m_defines;
  }
  const std::map<std::string, Uniform>& GetUniforms() const {
    return m_uniforms;
  }
  const std::map<std::string, RageTexture*>& GetTextures() const {
    return m_textures;
  }
  const std::string& GetError() const { return m_error; }
  void SetError(const std::string& error) { m_error = error; }
  bool IsDirty() const { return m_dirty; }
  bool IsReleased() const { return m_released; }
  void RequestCompile() {
    if (!m_released) {
      m_dirty = true;
    }
  }
  void MarkCompiled(bool success, const std::string& error = std::string());
  void SetDefine(const std::string& name, const std::string& value);
  void ClearDefine(const std::string& name);
  void SetFloatUniform(
      const std::string& name, int arity, const std::vector<float>& values,
      bool matrix = false);
  void SetIntUniform(const std::string& name, const std::vector<int>& values);
  void SetTextureUniform(const std::string& name, RageTexture* texture);

  // Call before TEXTUREMAN and DISPLAY are destroyed. This also protects Lua
  // userdata which outlives its original actor and survives until Lua shutdown.
  static void ReleaseAllResources();
  static void InvalidateAll();
  virtual void PushSelf(lua_State* L);

 protected:
  void ClearTextures();
  std::string m_vertexPath, m_fragmentPath;
  std::string m_vertexSource, m_fragmentSource;
  std::map<std::string, std::string> m_defines;
  std::map<std::string, Uniform> m_uniforms;
  std::map<std::string, RageTexture*> m_textures;
  std::string m_error;
  bool m_dirty = true;
  bool m_released = false;
};

#endif
