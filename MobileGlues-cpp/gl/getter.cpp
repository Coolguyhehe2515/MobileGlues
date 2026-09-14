// MobileGlues - gl/getter.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include "getter.h"
#include "enable.h"
#include "../egl/context.h"
#include "buffer.h"
#include "texture.h"
#include <string>
#include <format>
#include <vector>
#include <random>
#include "FSR1/FSR1.h"
#include "log.h"
#include "mg.h"
#include "pixel.h"
#include "random_string_gen.h"
#include "../config/settings.h"

#define DEBUG 0

Version GLVersion;

namespace {

thread_local GLenum g_frontend_error = GL_NO_ERROR;

static bool g_mc1122_compat_profile = false;

static GLint get_profile_mask() {
    if (!g_current_ctx) {
        return GL_CONTEXT_CORE_PROFILE_BIT;
    }

    if (g_current_ctx->profile_mask != 0) {
        return g_current_ctx->profile_mask;
    }

    return GL_CONTEXT_CORE_PROFILE_BIT;
}

static GLint get_granted_major() {
    if (!g_current_ctx) {
        return GLVersion.Major;
    }

    if (g_current_ctx->client_type == EGL_OPENGL_API) {
        return g_current_ctx->granted_major;
    }

    GLint value = 0;
    GLES.glGetIntegerv(GL_MAJOR_VERSION, &value);

    if (value <= 0) {
        value = GLVersion.Major;
    }

    return value;
}

static GLint get_granted_minor() {
    if (!g_current_ctx) {
        return GLVersion.Minor;
    }

    if (g_current_ctx->client_type == EGL_OPENGL_API) {
        return g_current_ctx->granted_minor;
    }

    GLint value = 0;
    GLES.glGetIntegerv(GL_MINOR_VERSION, &value);

    if (value < 0) {
        value = GLVersion.Minor;
    }

    return value;
}

static GLint count_extensions(const char* extension_string) {
    if (!extension_string || !*extension_string) {
        return 0;
    }

    GLint count = 0;
    bool in_token = false;

    for (const char* p = extension_string; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\n') {
            in_token = false;
        } else if (!in_token) {
            in_token = true;
            ++count;
        }
    }

    return count;
}

}

void mg_set_mc1122_compat_profile(int enabled) {
    g_mc1122_compat_profile = enabled != 0;

    if (g_mc1122_compat_profile) {
        LOG_I("Minecraft 1.12.2 compatibility profile enabled")
    } else {
        LOG_I("Minecraft 1.12.2 compatibility profile disabled")
    }
}

int mg_get_mc1122_compat_profile() {
    return g_mc1122_compat_profile ? 1 : 0;
}

void mg_set_gl_error(GLenum error) {
    if (error == GL_NO_ERROR) return;

    if (g_frontend_error != GL_NO_ERROR) return;

    g_frontend_error = error;

    LOG_D("MobileGlues raised %s", glEnumToString(error))
}

void glGetIntegerv(GLenum pname, GLint* params) {
    LOG()

    if (!params) {
        mg_set_gl_error(GL_INVALID_VALUE);
        return;
    }

    LOG_D("glGetIntegerv, pname: %s", glEnumToString(pname))

    switch (pname) {
    case GL_NUM_EXTENSIONS + GL_BACKEND_GETTER_MG:
        GLES.glGetIntegerv(pname - GL_BACKEND_GETTER_MG, params);
        return;

    case GL_CONTEXT_PROFILE_MASK:
        (*params) = get_profile_mask();
        break;

    case GL_NUM_EXTENSIONS: {
        const GLubyte* ext_str = glGetString(GL_EXTENSIONS);
        (*params) = count_extensions((const char*)ext_str);
        break;
    }

    case GL_MAJOR_VERSION:
        (*params) = get_granted_major();
        break;

    case GL_MINOR_VERSION:
        (*params) = get_granted_minor();
        break;

    case GL_MAX_TEXTURE_IMAGE_UNITS: {
        GLint es_params = 16;

        GLES.glGetIntegerv(pname, &es_params);

        if (es_params <= 0) {
            es_params = 16;
        }

        CHECK_GL_ERROR(*params) = es_params * 2;
        break;
    }

    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: {
        GLint es_params = 32;

        GLES.glGetIntegerv(pname, &es_params);

        CHECK_GL_ERROR

        const int tracked = mg_max_texture_units();

        if (tracked > 0 && es_params > tracked) {
            (*params) = tracked;
        } else {
            (*params) = es_params;
        }

        break;
    }

    case GL_CONTEXT_FLAGS:
        (*params) = g_current_ctx ? g_current_ctx->context_flags : 0;
        break;

    case GL_ARRAY_BUFFER_BINDING:
    case GL_ATOMIC_COUNTER_BUFFER_BINDING:
    case GL_COPY_READ_BUFFER_BINDING:
    case GL_COPY_WRITE_BUFFER_BINDING:
    case GL_DRAW_INDIRECT_BUFFER_BINDING:
    case GL_DISPATCH_INDIRECT_BUFFER_BINDING:
    case GL_ELEMENT_ARRAY_BUFFER_BINDING:
    case GL_PIXEL_PACK_BUFFER_BINDING:
    case GL_PIXEL_UNPACK_BUFFER_BINDING:
    case GL_SHADER_STORAGE_BUFFER_BINDING:
    case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
    case GL_UNIFORM_BUFFER_BINDING:
        (*params) = (int)find_bound_buffer(pname);
        LOG_D("  -> %d", *params)
        break;

    case GL_VERTEX_ARRAY_BINDING:
        (*params) = (int)find_bound_array();
        break;

    case GL_DRAW_FRAMEBUFFER_BINDING: {
        GLES.glGetIntegerv(pname, params);

        if (FSR1_Context::g_renderFBO != 0 &&
            *params == (GLint)FSR1_Context::g_renderFBO) {
            *params = 0;
        }

        LOG_D("  -> %d", *params)
        break;
    }

    default: {
        GLboolean enabled = GL_FALSE;
        GLint ival = 0;

        if (mg_enable_query(pname, &enabled)) {
            (*params) = enabled ? 1 : 0;
            break;
        }

        if (mg_enable_query_int(pname, &ival)) {
            (*params) = ival;
            break;
        }

        if (mg_pixel_store_query_int(pname, params)) {
            LOG_D("  -> %d", *params)
            break;
        }

        GLES.glGetIntegerv(pname, params);

        LOG_D("  -> %d", *params)

        CHECK_GL_ERROR
        break;
    }
    }
}

GLenum glGetError() {
    LOG()

    const GLenum backend = GLES.glGetError();
    const GLenum frontend = g_frontend_error;

    g_frontend_error = GL_NO_ERROR;

    const GLenum swallowed =
        frontend != GL_NO_ERROR ? frontend : backend;

    if (swallowed != GL_NO_ERROR) {
        LOG_W(
            "glGetError -> %s, reported to the application as GL_NO_ERROR",
            glEnumToString(swallowed)
        )
    }

    return GL_NO_ERROR;
}

static std::string es_ext;

std::string GetExtensionsList() {
    return es_ext;
}

void InitGLESBaseExtensions() {
    std::vector<std::string> extensions;

    if (global_settings.hide_mg_env_level == HideMGEnvLevel::Disabled) {
        extensions.push_back("GL_MG_mobileglues");
        extensions.push_back("GL_MG_backend_string_getter_access");
        extensions.push_back("GL_MG_settings_string_dump");
    }

    const char* base_exts[] = {
        "GL_ARB_fragment_program",
        "GL_ARB_vertex_buffer_object",
        "GL_ARB_vertex_array_object",
        "GL_ARB_vertex_buffer",
        "GL_EXT_vertex_array",
        "GL_ARB_ES2_compatibility",
        "GL_ARB_ES3_compatibility",
        "GL_EXT_packed_depth_stencil",
        "GL_EXT_depth_texture",
        "GL_ARB_depth_texture",
        "GL_ARB_shading_language_100",
        "GL_ARB_imaging",
        "GL_ARB_draw_buffers_blend",
        "OpenGL15",
        "GL_ARB_shader_storage_buffer_object",
        "GL_ARB_shader_image_load_store",
        "GL_ARB_clear_texture",
        "GL_ARB_get_program_binary",
        "GL_ARB_separate_shader_objects",
        "GL_ARB_multi_bind",
        "GL_KHR_no_error"
    };

    extensions.insert(
        extensions.end(),
        std::begin(base_exts),
        std::end(base_exts)
    );

    if (global_settings.hide_mg_env_level >= HideMGEnvLevel::Level1) {
        for (int i = (int)extensions.size() - 1; i > 0; --i) {
            int j = rand() % (i + 1);
            std::swap(extensions[i], extensions[j]);
        }
    }

    es_ext.clear();

    for (const auto& ext : extensions) {
        es_ext += ext;
        es_ext += " ";
    }
}

void AppendExtension(const char* ext) {
    if (!ext || !*ext) {
        return;
    }

    es_ext += ext;
    es_ext += ' ';
}

std::string getBeforeThirdSpace(const std::string& str) {
    int spaceCount = 0;
    size_t endPos = 0;

    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == ' ') {
            spaceCount++;

            if (spaceCount == 3) {
                endPos = i;
                break;
            }
        }

        if (spaceCount < 3) {
            endPos = str.length();
        }
    }

    return str.substr(0, endPos);
}

std::string getGpuName() {
    const GLubyte* renderer = GLES.glGetString(GL_RENDERER);

    if (!renderer) {
        return "<unknown>";
    }

    std::string gpuName((const char*)renderer);

    if (gpuName.empty()) {
        return "<unknown>";
    }

    if (gpuName.find("MetalANGLE, ANGLE") != std::string::npos) {
        if (gpuName.length() < 25) {
            return gpuName;
        }

        std::string gpu = gpuName.substr(23, gpuName.length() - 24);
        return gpu + " | MetalANGLE | Metal";
    }

    if (gpuName.rfind("ANGLE", 0) == 0 &&
        gpuName.find("Vulkan") != std::string::npos) {

        size_t firstParen = gpuName.find('(');
        size_t secondParen = gpuName.find('(', firstParen + 1);
        size_t lastParen = gpuName.rfind('(');

        if (firstParen != std::string::npos &&
            secondParen != std::string::npos &&
            lastParen != std::string::npos &&
            lastParen > secondParen) {

            std::string gpu =
                gpuName.substr(
                    secondParen + 1,
                    lastParen - secondParen - 2
                );

            size_t vulkanStart = gpuName.find("Vulkan ");

            if (vulkanStart != std::string::npos) {
                size_t vulkanEnd =
                    gpuName.find(' ', vulkanStart + 7);

                std::string vulkanVersion;

                if (vulkanEnd == std::string::npos) {
                    vulkanVersion =
                        gpuName.substr(vulkanStart + 7);
                } else {
                    vulkanVersion =
                        gpuName.substr(
                            vulkanStart + 7,
                            vulkanEnd - (vulkanStart + 7)
                        );
                }

                return gpu + " | ANGLE | Vulkan " + vulkanVersion;
            }
        }
    }

    return gpuName;
}

void set_es_version() {
    const GLubyte* version = GLES.glGetString(GL_VERSION);

    if (!version) {
        hardware->es_version = 300;
        return;
    }

    std::string ESVersionStr =
        getBeforeThirdSpace(std::string((const char*)version));

    int major = 0;
    int minor = 0;

    if (sscanf(
            ESVersionStr.c_str(),
            "OpenGL ES %d.%d",
            &major,
            &minor
        ) == 2) {

        hardware->es_version =
            major * 100 + minor * 10;
    } else {
        hardware->es_version = 300;
    }

    LOG_I(
        "OpenGL ES Version: %s (%d)",
        ESVersionStr.c_str(),
        hardware->es_version
    )

    if (hardware->es_version < 300) {
        LOG_I(
            "OpenGL ES version is lower than 3.0! This version is not supported!"
        )
    }
}

std::string getGLESName() {
    const GLubyte* version = GLES.glGetString(GL_VERSION);

    if (!version) {
        return "<unknown>";
    }

    return getBeforeThirdSpace(
        std::string((const char*)version)
    );
}

static std::string rendererString;
static std::string vendorString;
static std::string versionString;

const GLubyte* glGetString(GLenum name) {
    LOG()

    LOG_D(
        "glGetString, %s",
        glEnumToString(name)
    )

    switch (name) {
    case GL_VENDOR: {
        if (vendorString.empty()) {
            if (global_settings.hide_mg_env_level ==
                HideMGEnvLevel::Disabled) {

                vendorString =
                    "Swung0x48, BZLZHH, Tungsten";

            } else {
                const char choices[] = "AIN";

                vendorString =
                    std::string(1, choices[rand() % 3]);

                RandomStringOptions randStrOpts;

                randStrOpts.includeDigits = false;
                randStrOpts.minLength = 3;
                randStrOpts.maxLength = 8;
                randStrOpts.includeLowercase = false;
                randStrOpts.includeUppercase = false;
                randStrOpts.customChars = "IMenaNtMseAVlD";

                vendorString +=
                    GenerateRandomString(randStrOpts);
            }
        }

        return (const GLubyte*)vendorString.c_str();
    }

    case GL_VERSION: {
        if (versionString.empty()) {
            versionString = GLVersion.toString();

            if (global_settings.hide_mg_env_level ==
                HideMGEnvLevel::Disabled) {

                if (GLVersion.toInt(2) == DEFAULT_GL_VERSION) {
                    versionString += " MobileGlues ";
                } else {
                    Version defaultVersion =
                        Version(DEFAULT_GL_VERSION);

                    versionString +=
                        " §4§l(" +
                        defaultVersion.toString() +
                        ") MobileGlues§r ";
                }

                versionString +=
                    std::to_string(MAJOR) +
                    "." +
                    std::to_string(MINOR) +
                    "." +
                    std::to_string(REVISION);

#if PATCH != 0
                versionString +=
                    "." +
                    std::to_string(PATCH);
#endif

#if defined(VERSION_TYPE)

#if VERSION_TYPE == VERSION_ALPHA
                versionString += "·Alpha";
#elif VERSION_TYPE == VERSION_BETA
                versionString += "·Beta";
#elif VERSION_TYPE == VERSION_DEVELOPMENT
                versionString +=
                    "·Dev" +
                    std::to_string(VERSION_DEV_NUMBER);
#elif VERSION_TYPE == VERSION_RC
                versionString +=
                    "·RC" +
                    std::to_string(VERSION_RC_NUMBER);
#endif

                versionString += VERSION_SUFFIX;

#endif

            } else {
                const char choices[] = "AIN";

                versionString += " ";

                versionString +=
                    choices[rand() % 3];

                RandomStringOptions randStrOpts;

                randStrOpts.includeDigits = false;
                randStrOpts.customChars = " ";

                versionString +=
                    GenerateRandomString(randStrOpts);

                RandomStringOptions randStrOpts2;

                randStrOpts2.includeDigits = false;
                randStrOpts2.includeUppercase = false;
                randStrOpts2.minLength = 1;
                randStrOpts2.maxLength = 4;

                versionString +=
                    std::to_string(MAJOR) +
                    GenerateRandomString(randStrOpts2) +
                    std::to_string(MINOR) +
                    GenerateRandomString(randStrOpts2) +
                    std::to_string(REVISION) +
                    GenerateRandomString(randStrOpts2) +
                    std::to_string(PATCH) +
                    GenerateRandomString(randStrOpts2);
            }
        }

        return (const GLubyte*)versionString.c_str();
    }

    case GL_RENDERER: {
        if (rendererString.empty()) {
            if (global_settings.hide_mg_env_level ==
                HideMGEnvLevel::Disabled) {

                std::string gpuName = getGpuName();
                std::string glesName = getGLESName();

                rendererString =
                    gpuName +
                    " | " +
                    glesName;

            } else {
                const char choices[] = "AIN";

                rendererString =
                    std::string(1, choices[rand() % 3]);

                RandomStringOptions randStrOpts;

                randStrOpts.includeDigits = true;
                randStrOpts.minLength = 6;
                randStrOpts.maxLength = 12;
                randStrOpts.includeLowercase = false;
                randStrOpts.includeUppercase = false;
                randStrOpts.customChars =
                    "IRMenaNtfsoerAceVlDG";

                rendererString +=
                    GenerateRandomString(randStrOpts);

                int junkInfoTime =
                    rand() % 3 + 1;

                for (int i = 0; i < junkInfoTime; ++i) {
                    rendererString += " ";

                    RandomStringOptions randStrOpts2;

                    randStrOpts2.minLength = 3;
                    randStrOpts2.maxLength = 6;
                    randStrOpts2.includeLowercase = false;
                    randStrOpts2.includeUppercase = false;
                    randStrOpts2.customChars =
                        "IRenaNtfsoerAcieVDcsG";

                    rendererString +=
                        GenerateRandomString(randStrOpts2);
                }
            }
        }

        return (const GLubyte*)rendererString.c_str();
    }

    case GL_SHADING_LANGUAGE_VERSION: {
        static std::string shadingLangString;

        if (shadingLangString.empty()) {
            std::string baseVer;

            if (hardware->es_version < 310) {
                baseVer = "4.00";
            } else {
                baseVer = "4.60";
            }

            if (global_settings.hide_mg_env_level >=
                HideMGEnvLevel::Level1) {

                shadingLangString = baseVer;

                int junkCount =
                    rand() % 2 + 1;

                for (int i = 0; i < junkCount; ++i) {
                    shadingLangString += " ";

                    RandomStringOptions junkOpts;

                    junkOpts.minLength = 2;
                    junkOpts.maxLength = 5;
                    junkOpts.includeLowercase = false;
                    junkOpts.includeUppercase = false;
                    junkOpts.customChars =
                        "IAneNDtVsaMIl";

                    shadingLangString +=
                        GenerateRandomString(junkOpts);
                }

            } else {
                shadingLangString =
                    baseVer +
                    " MobileGlues with glslang and SPIRV-Cross";
            }
        }

        return reinterpret_cast<const GLubyte*>(
            shadingLangString.c_str()
        );
    }

    case GL_EXTENSIONS: {
        static std::string extensionsString;

        if (extensionsString.empty()) {
            extensionsString = GetExtensionsList();
        }

        return (const GLubyte*)extensionsString.c_str();
    }

    case GL_SETTINGS_MG: {
        if (global_settings.hide_mg_env_level >=
            HideMGEnvLevel::Level1) {
            return GLES.glGetString(name);
        }

        static char* settings_string = nullptr;

        std::string tmp =
            dump_settings_string("  ");

        if (settings_string) {
            free(settings_string);
        }

        settings_string =
            strdup(tmp.c_str());

        return reinterpret_cast<const GLubyte*>(
            settings_string
        );
    }

    case GL_VERSION + GL_BACKEND_GETTER_MG:
    case GL_VENDOR + GL_BACKEND_GETTER_MG:
    case GL_RENDERER + GL_BACKEND_GETTER_MG:
    case GL_EXTENSIONS + GL_BACKEND_GETTER_MG:
    case GL_SHADING_LANGUAGE_VERSION + GL_BACKEND_GETTER_MG:
        if (global_settings.hide_mg_env_level ==
            HideMGEnvLevel::Disabled) {

            return GLES.glGetString(
                name - GL_BACKEND_GETTER_MG
            );

        } else {
            return GLES.glGetString(name);
        }

    default:
        return GLES.glGetString(name);
    }
}

const GLubyte* glGetStringi(GLenum name, GLuint index) {
    LOG()

    if (name ==
            GL_EXTENSIONS + GL_BACKEND_GETTER_MG &&
        global_settings.hide_mg_env_level ==
            HideMGEnvLevel::Disabled) {

        return GLES.glGetStringi(
            name - GL_BACKEND_GETTER_MG,
            index
        );
    }

    typedef struct {
        GLenum name;
        const char** parts;
        GLuint count;
    } StringCache;

    static StringCache caches[] = {
        {GL_EXTENSIONS, nullptr, 0},
        {GL_VENDOR, nullptr, 0},
        {GL_VERSION, nullptr, 0},
        {GL_SHADING_LANGUAGE_VERSION, nullptr, 0}
    };

    static int initialized = 0;

    if (!initialized) {
        for (auto& cache : caches) {
            GLenum target = cache.name;
            const GLubyte* str = nullptr;
            const char* delimiter = " ";

            switch (target) {
            case GL_VENDOR:
                str = glGetString(GL_VENDOR);
                delimiter = ", ";
                break;

            case GL_VERSION:
                str = glGetString(GL_VERSION);
                delimiter = " .";
                break;

            case GL_SHADING_LANGUAGE_VERSION:
                str = glGetString(
                    GL_SHADING_LANGUAGE_VERSION
                );
                break;

            case GL_EXTENSIONS:
                str = glGetString(GL_EXTENSIONS);
                break;

            default:
                return GLES.glGetStringi(
                    name,
                    index
                );
            }

            if (!str) {
                continue;
            }

            std::string copy_str(
                (const char*)str
            );

            std::string token_str;
            size_t start = 0;
            size_t end =
                copy_str.find_first_of(
                    delimiter
                );

            while (end != std::string::npos) {
                if (end > start) {
                    token_str =
                        copy_str.substr(
                            start,
                            end - start
                        );

                    cache.parts =
                        (const char**)realloc(
                            cache.parts,
                            (cache.count + 1) *
                            sizeof(char*)
                        );

                    cache.parts[
                        cache.count++
                    ] = strdup(
                        token_str.c_str()
                    );
                }

                start = end + 1;

                end =
                    copy_str.find_first_of(
                        delimiter,
                        start
                    );
            }

            if (start < copy_str.size()) {
                token_str =
                    copy_str.substr(start);

                cache.parts =
                    (const char**)realloc(
                        cache.parts,
                        (cache.count + 1) *
                        sizeof(char*)
                    );

                cache.parts[
                    cache.count++
                ] = strdup(
                    token_str.c_str()
                );
            }
        }

        initialized = 1;
    }

    for (auto& cache : caches) {
        if (cache.name == name) {
            if (index >= cache.count) {
                return nullptr;
            }

            return (const GLubyte*)
                cache.parts[index];
        }
    }

    return nullptr;
}

void glGetQueryObjectiv(
    GLuint id,
    GLenum pname,
    GLint* params
) {
    LOG()

    if (!params) {
        mg_set_gl_error(GL_INVALID_VALUE);
        return;
    }

    if (GLES.glGetQueryObjectivEXT) {
        GLES.glGetQueryObjectivEXT(
            id,
            pname,
            params
        );

        CHECK_GL_ERROR
    }
}

void glGetQueryObjecti64v(
    GLuint id,
    GLenum pname,
    GLint64* params
) {
    LOG()

    if (!params) {
        mg_set_gl_error(GL_INVALID_VALUE);
        return;
    }

    if (GLES.glGetQueryObjecti64vEXT) {
        GLES.glGetQueryObjecti64vEXT(
            id,
            pname,
            params
        );

        CHECK_GL_ERROR
    }
}
