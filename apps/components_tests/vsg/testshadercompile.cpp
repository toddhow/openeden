#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <vsg/all.h>

#include <string>

TEST(VsgShaderCompilerTest, should_compile_glsl_vertex_shader_to_spirv)
{
    const std::string vertexShaderSource =
        "#version 450\n"
        "layout(location = 0) in vec3 inPosition;\n"
        "void main() {\n"
        "    gl_Position = vec4(inPosition, 1.0);\n"
        "}\n";

    auto compileSettings = vsg::ShaderCompileSettings::create();
    auto shaderStage = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", vertexShaderSource, compileSettings);
    ASSERT_NE(shaderStage, nullptr);

    auto compiler = vsg::ShaderCompiler::create();
    ASSERT_NE(compiler, nullptr);

    vsg::ShaderStages stages{ shaderStage };
    const bool compiled = compiler->compile(stages);
    EXPECT_TRUE(compiled);

    ASSERT_NE(shaderStage->module, nullptr);
    EXPECT_FALSE(shaderStage->module->code.empty());
    EXPECT_GT(shaderStage->module->code.size(), 0u);
}

#endif // OPENEDEN_BUILD_VSG
