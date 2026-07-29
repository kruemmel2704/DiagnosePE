// dear imgui: Renderer Backend for OpenGL3 / OpenGL 2.1 / ES
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <stdint.h>
#include <cstddef>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

struct ImGui_ImplOpenGL3_Data {
    GLuint          GlVersion;
    char            GlslVersionString[32];
    GLuint          FontTexture;
    GLuint          ShaderHandle;
    GLint           AttribLocPos;
    GLint           AttribLocUv;
    GLint           AttribLocColor;
    GLint           AttribLocProjMtx;
    GLuint          VboHandle, ElementsHandle;
};

static ImGui_ImplOpenGL3_Data* ImGui_ImplOpenGL3_GetBackendData() {
    return ImGui::GetCurrentContext() ? (ImGui_ImplOpenGL3_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

bool ImGui_ImplOpenGL3_Init(const char* glsl_version) {
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized!");

    ImGui_ImplOpenGL3_Data* bd = IM_NEW(ImGui_ImplOpenGL3_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_opengl3";

    if (glsl_version == nullptr)
        glsl_version = "#version 120";
    snprintf(bd->GlslVersionString, sizeof(bd->GlslVersionString), "%s", glsl_version);

    return true;
}

void ImGui_ImplOpenGL3_Shutdown() {
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown!");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    io.BackendRendererUserData = nullptr;
    IM_DELETE(bd);
}

void ImGui_ImplOpenGL3_NewFrame() {
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplOpenGL3_Init()?");

    if (!bd->ShaderHandle)
        ImGui_ImplOpenGL3_CreateDeviceObjects();
}

static void ImGui_ImplOpenGL3_SetupRenderState(ImDrawData* draw_data, int fb_width, int fb_height) {
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);

    glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

    const float ortho_projection[4][4] = {
        { 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
        { 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
        { 0.0f,         0.0f,        -1.0f,   0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
    };

    glUseProgram(bd->ShaderHandle);
    glUniform1i(glGetUniformLocation(bd->ShaderHandle, "Texture"), 0);
    glUniformMatrix4fv(bd->AttribLocProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, bd->VboHandle);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bd->ElementsHandle);

    glEnableVertexAttribArray(bd->AttribLocPos);
    glEnableVertexAttribArray(bd->AttribLocUv);
    glEnableVertexAttribArray(bd->AttribLocColor);

    glVertexAttribPointer(bd->AttribLocPos,   2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, pos));
    glVertexAttribPointer(bd->AttribLocUv,    2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, uv));
    glVertexAttribPointer(bd->AttribLocColor, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, col));
}

void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data) {
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;

    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();

    ImGui_ImplOpenGL3_SetupRenderState(draw_data, fb_width, fb_height);

    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * (int)sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * (int)sizeof(ImDrawIdx), (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmd_list, pcmd);
            } else {
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                if (clip_min.x < fb_width && clip_min.y < fb_height && clip_max.x >= 0.0f && clip_max.y >= 0.0f) {
                    glScissor((int)clip_min.x, (int)(fb_height - clip_max.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y));
                    glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID());
                    glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)));
                }
            }
        }
    }
}

bool ImGui_ImplOpenGL3_CreateFontsTexture() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    GLint last_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGenTextures(1, &bd->FontTexture);
    glBindTexture(GL_TEXTURE_2D, bd->FontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    io.Fonts->SetTexID((ImTextureID)(intptr_t)bd->FontTexture);
    glBindTexture(GL_TEXTURE_2D, last_texture);

    return true;
}

void ImGui_ImplOpenGL3_DestroyFontsTexture() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();
    if (bd->FontTexture) {
        glDeleteTextures(1, &bd->FontTexture);
        io.Fonts->SetTexID(0);
        bd->FontTexture = 0;
    }
}

bool ImGui_ImplOpenGL3_CreateDeviceObjects() {
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();

    const GLchar* vertex_shader =
        "#version 120\n"
        "uniform mat4 ProjMtx;\n"
        "attribute vec2 Position;\n"
        "attribute vec2 UV;\n"
        "attribute vec4 Color;\n"
        "varying vec2 Frag_UV;\n"
        "varying vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";

    const GLchar* fragment_shader =
        "#version 120\n"
        "uniform sampler2D Texture;\n"
        "varying vec2 Frag_UV;\n"
        "varying vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV);\n"
        "}\n";

    bd->ShaderHandle = glCreateProgram();
    GLuint vert_handle = glCreateShader(GL_VERTEX_SHADER);
    GLuint frag_handle = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vert_handle, 1, &vertex_shader, nullptr);
    glShaderSource(frag_handle, 1, &fragment_shader, nullptr);
    glCompileShader(vert_handle);
    glCompileShader(frag_handle);
    glAttachShader(bd->ShaderHandle, vert_handle);
    glAttachShader(bd->ShaderHandle, frag_handle);
    glLinkProgram(bd->ShaderHandle);

    bd->AttribLocProjMtx = glGetUniformLocation(bd->ShaderHandle, "ProjMtx");
    bd->AttribLocPos = glGetAttribLocation(bd->ShaderHandle, "Position");
    bd->AttribLocUv = glGetAttribLocation(bd->ShaderHandle, "UV");
    bd->AttribLocColor = glGetAttribLocation(bd->ShaderHandle, "Color");

    glGenBuffers(1, &bd->VboHandle);
    glGenBuffers(1, &bd->ElementsHandle);

    ImGui_ImplOpenGL3_CreateFontsTexture();
    return true;
}

void ImGui_ImplOpenGL3_DestroyDeviceObjects() {
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();
    if (bd->VboHandle) { glDeleteBuffers(1, &bd->VboHandle); bd->VboHandle = 0; }
    if (bd->ElementsHandle) { glDeleteBuffers(1, &bd->ElementsHandle); bd->ElementsHandle = 0; }
    if (bd->ShaderHandle) { glDeleteProgram(bd->ShaderHandle); bd->ShaderHandle = 0; }
    ImGui_ImplOpenGL3_DestroyFontsTexture();
}
