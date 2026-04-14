/* hxediter — Dear ImGui hex editor (GLFW + OpenGL3) */

#include "hex_editor_core.h"
#include "gui.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

static void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    /* ---- Open the file via the model ---- */
    HexEditorCore* core = nullptr;
    try {
        core = new HexEditorCore(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    /* ---- GLFW init ---- */
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        delete core;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    std::string title = "hxediter — " + core->GetFilename();
    GLFWwindow* window = glfwCreateWindow(1280, 720, title.c_str(), nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        delete core;
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); /* vsync */

    /* ---- Dear ImGui init ---- */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    /* Load Consolas if available, else fall back to default */
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 16.0f);
    if (!font) {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    /* ---- Render loop ---- */
    ImVec4 clear_color = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderHexEditorUI(*core);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    /* ---- Cleanup ---- */
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    delete core;
    return 0;
}
