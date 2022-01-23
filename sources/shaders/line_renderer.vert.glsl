R"(
    #version 440 core

    layout(location = 0)in vec2 a_Position;
    layout(location = 1)in vec4 a_Color;

    layout(location = 0)out vec4 v_Color;

    layout(std140, binding = 0)uniform MatricesUniform{
        mat4 u_Projection;
    };

    void main(){
        gl_Position = u_Projection * vec4(a_Position.xy, 0.0, 1.0);

        v_Color = a_Color;
    }
)"