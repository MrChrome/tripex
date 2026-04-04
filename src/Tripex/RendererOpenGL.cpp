#include "RendererOpenGL.h"
#include "Platform.h"
#include "Vertex.h"
#include "Face.h"
#include "Error.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <cassert>

// ---------------------------------------------------------------------------
// GLSL shaders (OpenGL 3.2 Core Profile / GLSL 150)
// ---------------------------------------------------------------------------

static const char* VERT_SRC = R"(
#version 150 core

uniform vec2 screen_size;

in vec3  in_position;
in float in_rhw;
in vec4  in_diffuse;
in vec4  in_specular;
in vec2  in_tex_coord;

out vec4 frag_diffuse;
out vec4 frag_specular;
out vec2 frag_tex_coord;

void main()
{
    // Convert pixel-space coords (origin = top-left, y-down) to NDC
    float x_ndc =  2.0 * in_position.x / screen_size.x - 1.0;
    float y_ndc = -2.0 * in_position.y / screen_size.y + 1.0;
    // D3D depth [0,1] → OpenGL NDC [-1,1]
    float z_ndc =  2.0 * in_position.z - 1.0;
    gl_Position = vec4(x_ndc, y_ndc, z_ndc, 1.0);

    // Attributes arrive as normalised bytes in BGRA order; swizzle to RGBA.
    frag_diffuse  = vec4(in_diffuse.z,  in_diffuse.y,  in_diffuse.x,  in_diffuse.w);
    frag_specular = vec4(in_specular.z, in_specular.y, in_specular.x, in_specular.w);
    frag_tex_coord = in_tex_coord;
}
)";

static const char* FRAG_SRC = R"(
#version 150 core

uniform sampler2D tex;
uniform bool has_texture;
uniform bool enable_specular;

in vec4 frag_diffuse;
in vec4 frag_specular;
in vec2 frag_tex_coord;

out vec4 out_color;

void main()
{
    vec4 color;
    if (has_texture)
        color = texture(tex, frag_tex_coord) * frag_diffuse;
    else
        color = frag_diffuse;

    if (enable_specular)
        color.rgb += frag_specular.rgb;

    out_color = color;
}
)";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Error* MakeGLError(const char* msg)
{
	return new Error(std::string(msg));
}

GLuint RendererOpenGL::CompileShader(GLenum type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[1024] = {};
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		fprintf(stderr, "Shader compile error: %s\n", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

Error* RendererOpenGL::CreateProgram(GLuint& out_program)
{
	GLuint vert = CompileShader(GL_VERTEX_SHADER,   VERT_SRC);
	GLuint frag = CompileShader(GL_FRAGMENT_SHADER, FRAG_SRC);
	if (!vert || !frag)
		return MakeGLError("Failed to compile shaders");

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);

	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[1024] = {};
		glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
		glDeleteProgram(prog);
		return new Error(std::string("Shader link error: ") + log);
	}

	out_program = prog;
	return nullptr;
}

// ---------------------------------------------------------------------------
// TextureImpl
// ---------------------------------------------------------------------------

RendererOpenGL::TextureImpl::TextureImpl(int w, int h, TextureFormat fmt, TextureFlags flags)
	: Texture(w, h, fmt, flags)
	, gl_texture(0)
	, src_data(nullptr)
	, src_stride(0)
	, src_palette(nullptr)
	, dirty(false)
{
}

RendererOpenGL::TextureImpl::~TextureImpl()
{
	if (gl_texture)
		glDeleteTextures(1, &gl_texture);
}

void RendererOpenGL::TextureImpl::SetDirty()
{
	dirty = true;
}

Error* RendererOpenGL::TextureImpl::GetPixelData(std::vector<uint8>& buffer) const
{
	glBindTexture(GL_TEXTURE_2D, gl_texture);
	buffer.resize(width * height * 4);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	return nullptr;
}

// ---------------------------------------------------------------------------
// RendererOpenGL
// ---------------------------------------------------------------------------

RendererOpenGL::RendererOpenGL()
	: sdl_window(nullptr)
	, gl_context(nullptr)
	, screen_width(0)
	, screen_height(0)
	, program(0)
	, vao(0), vbo(0), ebo(0)
	, u_screen_size(-1), u_has_texture(-1), u_enable_specular(-1)
	, a_position(-1), a_rhw(-1), a_diffuse(-1), a_specular(-1), a_tex_coord(-1)
{
}

RendererOpenGL::~RendererOpenGL()
{
	Close();
}

Error* RendererOpenGL::Open(SDL_Window* window, SDL_GLContext context, int width, int height)
{
	sdl_window  = window;
	gl_context  = context;
	screen_width  = width;
	screen_height = height;

	Error* err = CreateProgram(program);
	if (err) return err;

	glUseProgram(program);

	// Uniforms
	u_screen_size    = glGetUniformLocation(program, "screen_size");
	u_has_texture    = glGetUniformLocation(program, "has_texture");
	u_enable_specular = glGetUniformLocation(program, "enable_specular");

	// Attributes
	a_position  = glGetAttribLocation(program, "in_position");
	a_rhw       = glGetAttribLocation(program, "in_rhw");
	a_diffuse   = glGetAttribLocation(program, "in_diffuse");
	a_specular  = glGetAttribLocation(program, "in_specular");
	a_tex_coord = glGetAttribLocation(program, "in_tex_coord");

	// VAO / VBO / EBO
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	// VertexTL layout:
	//   offset  0: position  (3 floats = 12 bytes)
	//   offset 12: rhw       (1 float  =  4 bytes)
	//   offset 16: diffuse   (4 uint8  =  4 bytes, BGRA)
	//   offset 20: specular  (4 uint8  =  4 bytes, BGRA)
	//   offset 24: tex_coord (2 floats =  8 bytes)
	//   total: 32 bytes
	const GLsizei stride = 32;

	glEnableVertexAttribArray(a_position);
	glVertexAttribPointer(a_position, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);

	glEnableVertexAttribArray(a_rhw);
	glVertexAttribPointer(a_rhw, 1, GL_FLOAT, GL_FALSE, stride, (void*)12);

	glEnableVertexAttribArray(a_diffuse);
	glVertexAttribPointer(a_diffuse, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)16);

	glEnableVertexAttribArray(a_specular);
	glVertexAttribPointer(a_specular, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)20);

	glEnableVertexAttribArray(a_tex_coord);
	glVertexAttribPointer(a_tex_coord, 2, GL_FLOAT, GL_FALSE, stride, (void*)24);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

	glBindVertexArray(0);

	// Depth buffer setup
	glEnable(GL_DEPTH_TEST);

	// Set texture sampler unit 0
	glUniform1i(glGetUniformLocation(program, "tex"), 0);

	return nullptr;
}

void RendererOpenGL::Close()
{
	if (ebo)    { glDeleteBuffers(1, &ebo);       ebo = 0; }
	if (vbo)    { glDeleteBuffers(1, &vbo);       vbo = 0; }
	if (vao)    { glDeleteVertexArrays(1, &vao);  vao = 0; }
	if (program){ glDeleteProgram(program);       program = 0; }
}

void RendererOpenGL::Resize(int width, int height)
{
	screen_width  = width;
	screen_height = height;
	glViewport(0, 0, width, height);
}

Error* RendererOpenGL::BeginFrame()
{
	glViewport(0, 0, screen_width, screen_height);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(program);
	glUniform2f(u_screen_size, (float)screen_width, (float)screen_height);

	return nullptr;
}

Error* RendererOpenGL::EndFrame()
{
	SDL_GL_SwapWindow(sdl_window);
	return nullptr;
}

Rect<int> RendererOpenGL::GetViewportRect() const
{
	return Rect<int>(0, 0, screen_width, screen_height);
}

Rect<float> RendererOpenGL::GetClipRect() const
{
	// No guard band on OpenGL; use a generous clip region matching the viewport
	return Rect<float>(-0.25f, -0.25f,
	                   (float)screen_width  - 0.25f,
	                   (float)screen_height - 0.25f);
}

// ---------------------------------------------------------------------------
// Texture upload helpers
// ---------------------------------------------------------------------------

void RendererOpenGL::UploadTexture(TextureImpl* tex, const void* data, uint32 data_stride, const ColorRgb* palette)
{
	glBindTexture(GL_TEXTURE_2D, tex->gl_texture);

	if (tex->format == TextureFormat::P8)
	{
		// Expand 8-bit palette-indexed data to RGBA
		std::vector<uint8> rgba(tex->width * tex->height * 4);
		const uint8* src_row = (const uint8*)data;
		uint8* dst = rgba.data();
		for (int y = 0; y < tex->height; y++)
		{
			const uint8* src = src_row;
			for (int x = 0; x < tex->width; x++)
			{
				uint8 idx = *src++;
				const ColorRgb& c = palette ? palette[idx] : ColorRgb(idx, idx, idx);
				*dst++ = c.r;
				*dst++ = c.g;
				*dst++ = c.b;
				*dst++ = c.a;
			}
			src_row += data_stride;
		}
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex->width, tex->height,
		             0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	}
	else // X8R8G8B8 — stored as BGRA bytes
	{
		if (data_stride == (uint32)(tex->width * 4))
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex->width, tex->height,
			             0, GL_BGRA, GL_UNSIGNED_BYTE, data);
		}
		else
		{
			// Non-contiguous rows — copy to a temp buffer
			std::vector<uint8> tmp(tex->width * tex->height * 4);
			const uint8* src_row = (const uint8*)data;
			uint8* dst = tmp.data();
			for (int y = 0; y < tex->height; y++)
			{
				memcpy(dst, src_row, tex->width * 4);
				dst     += tex->width * 4;
				src_row += data_stride;
			}
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex->width, tex->height,
			             0, GL_BGRA, GL_UNSIGNED_BYTE, tmp.data());
		}
	}

	bool create_mips = (tex->flags & TextureFlags::CreateMips) == TextureFlags::CreateMips;
	bool filter      = (tex->flags & TextureFlags::Filter)     == TextureFlags::Filter;

	if (create_mips)
		glGenerateMipmap(GL_TEXTURE_2D);

	if (filter)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		                create_mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		                create_mips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}

Error* RendererOpenGL::CreateTexture(int width, int height, TextureFormat format,
                                     const void* data, uint32 data_size, uint32 data_stride,
                                     const ColorRgb* palette, TextureFlags flags,
                                     std::shared_ptr<Texture>& out_texture)
{
	auto tex = std::make_shared<TextureImpl>(width, height, format, flags);

	glGenTextures(1, &tex->gl_texture);

	bool dynamic = (flags & TextureFlags::Dynamic) == TextureFlags::Dynamic;
	if (dynamic)
	{
		tex->src_data    = data;
		tex->src_stride  = data_stride;
		tex->src_palette = palette;
	}

	UploadTexture(tex.get(), data, data_stride, palette);

	out_texture = std::move(tex);
	return nullptr;
}

Error* RendererOpenGL::CreateTextureFromImage(const void* data, uint32 data_size,
                                              std::shared_ptr<Texture>& out_texture)
{
	int w, h, channels;
	unsigned char* pixels = stbi_load_from_memory(
	    (const stbi_uc*)data, (int)data_size, &w, &h, &channels, 4);

	if (!pixels)
		return new Error(std::string("stb_image failed: ") + stbi_failure_reason());

	auto tex = std::make_shared<TextureImpl>(w, h, TextureFormat::X8R8G8B8,
	                                         TextureFlags::CreateMips | TextureFlags::Filter);
	glGenTextures(1, &tex->gl_texture);
	glBindTexture(GL_TEXTURE_2D, tex->gl_texture);

	// stb_image gives us RGBA; upload as RGBA
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	stbi_image_free(pixels);

	out_texture = std::move(tex);
	return nullptr;
}

// ---------------------------------------------------------------------------
// DrawIndexedPrimitive
// ---------------------------------------------------------------------------

Error* RendererOpenGL::DrawIndexedPrimitive(const RenderState& rs,
                                            size_t num_vertices, const VertexTL* vertices,
                                            size_t num_faces,    const Face* faces)
{
	assert(num_vertices < 32768);

	// --- Culling ---
	if (rs.enable_culling)
	{
		glEnable(GL_CULL_FACE);
		// D3D default is CCW front face with CCW culling (cull back faces)
		glFrontFace(GL_CW);
		glCullFace(GL_BACK);
	}
	else
	{
		glDisable(GL_CULL_FACE);
	}

	// --- Depth ---
	switch (rs.depth_mode)
	{
	case DepthMode::Disable:
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		break;
	case DepthMode::Normal:
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		break;
	case DepthMode::Stencil:
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_EQUAL);
		glDepthMask(GL_FALSE);
		break;
	}

	// --- Blending ---
	switch (rs.blend_mode)
	{
	case BlendMode::NoOp:
		glEnable(GL_BLEND);
		glBlendFunc(GL_ZERO, GL_ONE);
		break;
	case BlendMode::Replace:
		glDisable(GL_BLEND);
		break;
	case BlendMode::Add:
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		break;
	case BlendMode::Tint:
		glEnable(GL_BLEND);
		glBlendFunc(GL_DST_COLOR, GL_ONE);
		break;
	case BlendMode::OverlayBackground:
		glEnable(GL_BLEND);
		glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
		break;
	case BlendMode::OverlayForeground:
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
		break;
	default:
		assert(false);
	}

	// --- Specular uniform ---
	glUniform1i(u_enable_specular, rs.enable_specular ? 1 : 0);

	// --- Texture ---
	const TextureStage& stage0 = rs.texture_stages[0];
	TextureImpl* tex = (TextureImpl*)stage0.texture;

	if (tex)
	{
		// Re-upload if dirty
		if (tex->dirty)
		{
			UploadTexture(tex, tex->src_data, tex->src_stride, tex->src_palette);
			tex->dirty = false;
		}

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex->gl_texture);
		glUniform1i(u_has_texture, 1);

		auto GetWrap = [](TextureAddress addr) -> GLint {
			return addr == TextureAddress::Wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;
		};
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GetWrap(stage0.address_u));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GetWrap(stage0.address_v));
	}
	else
	{
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glUniform1i(u_has_texture, 0);
	}

	// --- Upload geometry ---
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, num_vertices * sizeof(VertexTL), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_faces * sizeof(Face), faces, GL_STREAM_DRAW);

	glDrawElements(GL_TRIANGLES, (GLsizei)(num_faces * 3), GL_UNSIGNED_SHORT, nullptr);

	glBindVertexArray(0);

	return nullptr;
}
