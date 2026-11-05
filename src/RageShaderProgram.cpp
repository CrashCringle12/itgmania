#include "RageShaderProgram.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <set>

#include "PrefsManager.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageTexture.h"
#include "RageTextureManager.h"
#include "global.h"

namespace {
std::set<RageShaderProgram*>& Resources() {
  static std::set<RageShaderProgram*> resources;
  return resources;
}
bool ReadSource(
    const std::string& path, std::string& source, std::string& error) {
  source.clear();
  if (path.empty()) {
    return true;
  }
  RageFile file;
  if (!file.Open(path) || file.Read(source) < 0) {
    error = "Cannot read shader " + path + ": " + file.GetError();
    return false;
  }
  // Strip the UTF-8 BOM; GLSL requires #version to precede other tokens.
  if (source.compare(0, 3, "\xef\xbb\xbf") == 0) {
    source.erase(0, 3);
  }
  return true;
}
}  // namespace

bool RageShaderProgram::CustomShadersDisabled(const char* operation) {
  static std::set<std::string> warnedOperations;
  if (!PREFSMAN || !PREFSMAN->m_bCustomShadersDisabled) {
    warnedOperations.clear();
    return false;
  }
  // Uniforms may be updated every frame. Warn once per operation while
  // disabled.
  if (operation && LOG && warnedOperations.insert(operation).second) {
    LOG->Warn(
        "CustomShadersDisabled: ignoring custom shader operation %s",
        operation);
  }
  return true;
}

RageShaderProgram::RageShaderProgram() { Resources().insert(this); }
RageShaderProgram::~RageShaderProgram() {
  ClearTextures();
  Resources().erase(this);
}
void RageShaderProgram::SetSources(
    const std::string& vertexPath, const std::string& fragmentPath,
    const std::string& vertexSource, const std::string& fragmentSource) {
  if (CustomShadersDisabled("SetSources")) {
    return;
  }
  m_vertexPath = vertexPath;
  m_fragmentPath = fragmentPath;
  m_vertexSource = vertexSource;
  m_fragmentSource = fragmentSource;
  m_error.clear();
  RequestCompile();
}
bool RageShaderProgram::LoadSources(
    const std::string& vertexPath, const std::string& fragmentPath) {
  if (CustomShadersDisabled("LoadSources")) {
    return true;
  }
  std::string vertex, fragment, error;
  if (!ReadSource(vertexPath, vertex, error) ||
      !ReadSource(fragmentPath, fragment, error)) {
    m_error = error;
    return false;
  }
  SetSources(vertexPath, fragmentPath, vertex, fragment);
  return true;
}
void RageShaderProgram::MarkCompiled(bool success, const std::string& error) {
  m_dirty = false;
  m_error = error;
  if (!success && m_error.empty()) {
    m_error = "Shader compilation failed";
  }
}
void RageShaderProgram::SetDefine(
    const std::string& name, const std::string& value) {
  if (CustomShadersDisabled("SetDefine")) {
    return;
  }
  const std::map<std::string, std::string>::iterator it = m_defines.find(name);
  if (it != m_defines.end() && it->second == value) {
    return;
  }
  m_defines[name] = value;
  RequestCompile();
}
void RageShaderProgram::ClearDefine(const std::string& name) {
  if (CustomShadersDisabled("ClearDefine")) {
    return;
  }
  if (m_defines.erase(name)) {
    RequestCompile();
  }
}
void RageShaderProgram::SetFloatUniform(
    const std::string& name, int arity, const std::vector<float>& values,
    bool matrix) {
  if (CustomShadersDisabled("SetFloatUniform")) {
    return;
  }
  Uniform& uniform = m_uniforms[name];
  uniform.kind = matrix ? Uniform::Matrix : Uniform::Float;
  uniform.arity = arity;
  uniform.floats = values;
  uniform.integers.clear();
}
void RageShaderProgram::SetIntUniform(
    const std::string& name, const std::vector<int>& values) {
  if (CustomShadersDisabled("SetIntUniform")) {
    return;
  }
  Uniform& uniform = m_uniforms[name];
  uniform.kind = Uniform::Int;
  uniform.arity = 1;
  uniform.integers = values;
  uniform.floats.clear();
}
void RageShaderProgram::SetTextureUniform(
    const std::string& name, RageTexture* texture) {
  if (CustomShadersDisabled("SetTextureUniform")) {
    return;
  }
  if (m_released) {
    return;
  }
  std::map<std::string, RageTexture*>::iterator it = m_textures.find(name);
  if (it != m_textures.end() && it->second == texture) {
    return;
  }
  // Acquire the replacement before dropping the old reference.
  RageTexture* retained =
      texture && TEXTUREMAN ? TEXTUREMAN->CopyTexture(texture) : nullptr;
  if (it != m_textures.end()) {
    if (TEXTUREMAN) {
      TEXTUREMAN->UnloadTexture(it->second);
    }
    m_textures.erase(it);
  }
  if (retained) {
    m_textures[name] = retained;
  }
}
void RageShaderProgram::ClearTextures() {
  if (TEXTUREMAN) {
    for (const std::pair<const std::string, RageTexture*>& texture :
         m_textures) {
      TEXTUREMAN->UnloadTexture(texture.second);
    }
  }
  m_textures.clear();
}
void RageShaderProgram::InvalidateAll() {
  for (RageShaderProgram* resource : Resources()) {
    if (resource->m_released) {
      continue;
    }
    resource->Invalidate();
    resource->RequestCompile();
  }
}
void RageShaderProgram::ReleaseAllResources() {
  for (RageShaderProgram* resource : Resources()) {
    if (resource->m_released) {
      continue;
    }
    resource->ReleaseGPUResources();
    resource->ClearTextures();
    resource->m_released = true;
    resource->m_dirty = false;
  }
}

// Lua userdata owns a strong reference instead of relying on the originating
// actor to keep a raw pointer alive. Its leading pointer matches Luna dispatch.
#include "LuaBinding.h"

namespace {
struct ShaderUserdata {
  RageShaderProgram* pointer;
  std::shared_ptr<RageShaderProgram> owner;
};
float FloatArgument(lua_State* L, int argument) {
  return LuaHelpers::CheckFiniteFloat(L, argument, "Shader");
}
int IntegerArgument(lua_State* L, int argument) {
  const double value = luaL_checknumber(L, argument);
  if (!std::isfinite(value) || value != std::floor(value) ||
      value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max()) {
    luaL_error(L, "Shader argument %d must be a finite integer", argument);
  }
  return static_cast<int>(value);
}
size_t CheckArray(lua_State* L, int argument, int arity, bool integer) {
  luaL_checktype(L, argument, LUA_TTABLE);
  const size_t count = lua_objlen(L, argument);
  if (count == 0 || count > 1048576 || count % arity != 0) {
    luaL_error(
        L,
        "Shader uniform array must contain a nonzero multiple of %d values "
        "(maximum 1048576)",
        arity);
  }
  // Validate before allocating a vector, including on Lua builds using longjmp.
  for (size_t i = 0; i < count; ++i) {
    lua_rawgeti(L, argument, static_cast<int>(i + 1));
    if (integer) {
      IntegerArgument(L, -1);
    } else {
      FloatArgument(L, -1);
    }
    lua_pop(L, 1);
  }
  return count;
}
bool IsDefineName(const char* name) {
  if (!name[0] ||
      (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_')) {
    return false;
  }
  for (const char* p = name + 1; *p; ++p) {
    if (!std::isalnum(static_cast<unsigned char>(*p)) && *p != '_') {
      return false;
    }
  }
  return true;
}
}  // namespace

class LunaRageShaderProgram : public Luna<RageShaderProgram> {
 protected:
  void Register(Lua* L, int methods, int metatable) override {
    Luna<RageShaderProgram>::Register(L, methods, metatable);
    lua_pushcfunction(L, Collect);
    lua_setfield(L, metatable, "__gc");
    lua_pushcfunction(L, Equal);
    lua_setfield(L, metatable, "__eq");
  }
  static int Collect(lua_State* L) {
    ShaderUserdata* value = static_cast<ShaderUserdata*>(lua_touserdata(L, 1));
    if (value) {
      value->~ShaderUserdata();
    }
    return 0;
  }
  static int Equal(lua_State* L) {
    lua_pushboolean(
        L, Luna<RageShaderProgram>::check(L, 1) ==
               Luna<RageShaderProgram>::check(L, 2));
    return 1;
  }

 public:
  static int define(T* p, lua_State* L) {
    if (T::CustomShadersDisabled("define")) {
      COMMON_RETURN_SELF;
    }
    const char* name = luaL_checkstring(L, 1);
    const char* value = luaL_optstring(L, 2, "1");
    if (!IsDefineName(name)) {
      return luaL_error(L, "Invalid shader define name");
    }
    if (std::string(value).find_first_of("\r\n") != std::string::npos) {
      return luaL_error(L, "Shader define values must not contain line breaks");
    }
    p->SetDefine(name, value);
    COMMON_RETURN_SELF;
  }
  static int clearDefine(T* p, lua_State* L) {
    if (T::CustomShadersDisabled("clearDefine")) {
      COMMON_RETURN_SELF;
    }
    p->ClearDefine(luaL_checkstring(L, 1));
    COMMON_RETURN_SELF;
  }
  static int compile(T* p, lua_State* L) {
    if (T::CustomShadersDisabled("compile")) {
      COMMON_RETURN_SELF;
    }
    if (lua_gettop(L) != 0) {
      // Check both source strings before creating C++ strings on longjmp-based
      // Lua builds.
      size_t vertexLength = 0, fragmentLength = 0;
      const char* vertex = luaL_checklstring(L, 1, &vertexLength);
      const char* fragment = luaL_checklstring(L, 2, &fragmentLength);
      p->SetSources(
          "", "", std::string(vertex, vertexLength),
          std::string(fragment, fragmentLength));
    } else {
      // Extension: queue a rebuild of the existing sources and definitions.
      p->RequestCompile();
    }
    COMMON_RETURN_SELF;
  }
  static int compileImmediate(T* p, lua_State* L) {
    if (T::CustomShadersDisabled("compileImmediate")) {
      COMMON_RETURN_SELF;
    }
    p->RequestCompile();
    if (!p->Compile()) {
      LuaHelpers::ReportScriptError("Shader compilation: " + p->GetError());
    }
    COMMON_RETURN_SELF;
  }
  static int isValid(T* p, lua_State* L) {
    const bool disabled = T::CustomShadersDisabled("isValid");
    lua_pushboolean(L, !disabled && p->IsValid());
    return 1;
  }
  static int getError(T* p, lua_State* L) {
    const bool disabled = T::CustomShadersDisabled("getError");
    lua_pushstring(
        L,
        disabled ? "CustomShadersDisabled is enabled" : p->GetError().c_str());
    return 1;
  }
  static int FloatScalar(T* p, lua_State* L, int arity) {
    if (T::CustomShadersDisabled("float uniform")) {
      COMMON_RETURN_SELF;
    }
    const char* name = luaL_checkstring(L, 1);
    float values[4] = {};
    for (int i = 0; i < arity; ++i) {
      values[i] = FloatArgument(L, i + 2);
    }
    p->SetFloatUniform(name, arity, std::vector<float>(values, values + arity));
    COMMON_RETURN_SELF;
  }
  static int uniform1f(T* p, lua_State* L) { return FloatScalar(p, L, 1); }
  static int uniform2f(T* p, lua_State* L) { return FloatScalar(p, L, 2); }
  static int uniform3f(T* p, lua_State* L) { return FloatScalar(p, L, 3); }
  static int uniform4f(T* p, lua_State* L) { return FloatScalar(p, L, 4); }
  static int uniform1i(T* p, lua_State* L) {
    if (T::CustomShadersDisabled("uniform1i")) {
      COMMON_RETURN_SELF;
    }
    const char* name = luaL_checkstring(L, 1);
    const int value = IntegerArgument(L, 2);
    p->SetIntUniform(name, {value});
    COMMON_RETURN_SELF;
  }
  static int FloatArray(T* p, lua_State* L, int arity, bool matrix = false) {
    if (T::CustomShadersDisabled("float array or matrix uniform")) {
      COMMON_RETURN_SELF;
    }
    const char* name = luaL_checkstring(L, 1);
    const size_t count =
        CheckArray(L, 2, matrix ? arity * arity : arity, false);
    std::vector<float> values;
    values.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      lua_rawgeti(L, 2, static_cast<int>(i + 1));
      values.push_back(static_cast<float>(lua_tonumber(L, -1)));
      lua_pop(L, 1);
    }
    p->SetFloatUniform(name, arity, values, matrix);
    COMMON_RETURN_SELF;
  }
  static int uniform1fv(T* p, lua_State* L) { return FloatArray(p, L, 1); }
  static int uniform2fv(T* p, lua_State* L) { return FloatArray(p, L, 2); }
  static int uniform3fv(T* p, lua_State* L) { return FloatArray(p, L, 3); }
  static int uniform4fv(T* p, lua_State* L) { return FloatArray(p, L, 4); }
  static int uniformMatrix2fv(T* p, lua_State* L) {
    return FloatArray(p, L, 2, true);
  }
  static int uniformMatrix3fv(T* p, lua_State* L) {
    return FloatArray(p, L, 3, true);
  }
  static int uniformMatrix4fv(T* p, lua_State* L) {
    return FloatArray(p, L, 4, true);
  }
  static int uniform1iv(T* p, lua_State* L) {
    if (T::CustomShadersDisabled("uniform1iv")) {
      COMMON_RETURN_SELF;
    }
    const char* name = luaL_checkstring(L, 1);
    const size_t count = CheckArray(L, 2, 1, true);
    std::vector<int> values;
    values.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      lua_rawgeti(L, 2, static_cast<int>(i + 1));
      values.push_back(static_cast<int>(lua_tonumber(L, -1)));
      lua_pop(L, 1);
    }
    p->SetIntUniform(name, values);
    COMMON_RETURN_SELF;
  }
  static int uniformTexture(T* p, lua_State* L) {
    if (T::CustomShadersDisabled("uniformTexture")) {
      COMMON_RETURN_SELF;
    }
    const char* name = luaL_checkstring(L, 1);
    RageTexture* texture =
        lua_isnil(L, 2) ? nullptr : Luna<RageTexture>::check(L, 2);
    p->SetTextureUniform(name, texture);
    COMMON_RETURN_SELF;
  }
  LunaRageShaderProgram() {
    ADD_METHOD(define);
    ADD_METHOD(clearDefine);
    ADD_METHOD(compile);
    ADD_METHOD(compileImmediate);
    ADD_METHOD(uniform1f);
    ADD_METHOD(uniform2f);
    ADD_METHOD(uniform3f);
    ADD_METHOD(uniform4f);
    ADD_METHOD(uniform1i);
    ADD_METHOD(uniform1iv);
    ADD_METHOD(uniform1fv);
    ADD_METHOD(uniform2fv);
    ADD_METHOD(uniform3fv);
    ADD_METHOD(uniform4fv);
    ADD_METHOD(uniformMatrix2fv);
    ADD_METHOD(uniformMatrix3fv);
    ADD_METHOD(uniformMatrix4fv);
    ADD_METHOD(uniformTexture);
    ADD_METHOD(isValid);
    ADD_METHOD(getError);
  }
};

template <>
void Luna<RageShaderProgram>::PushObject(
    Lua* L, const std::string& derivedClassName, RageShaderProgram* program) {
  void* storage = lua_newuserdata(L, sizeof(ShaderUserdata));
  new (storage) ShaderUserdata{program, program->shared_from_this()};
  LuaBinding::ApplyDerivedType(L, derivedClassName, program);
}
LUA_REGISTER_CLASS_BASIC(RageShaderProgram, RageShaderProgram)
