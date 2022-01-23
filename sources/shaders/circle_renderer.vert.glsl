R"(
    #version 440 core

    layout(location = 0)in vec2 a_Position;
    layout(location = 1)in vec2 a_Center;
    layout(location = 2)in vec4 a_Color;
    layout(location = 3)in float a_Radius;

    layout(location = 0)out vec4 v_Color;
    layout(location = 1)out vec2 v_Position;
    layout(location = 2)out vec2 v_Center;
    layout(location = 3)out flat float v_Radius;

    layout(std140, binding = 0)uniform MatricesUniform{
        mat4 u_Projection;
    };

    void main(){
        gl_Position = u_Projection * vec4(a_Position.xy, 0.0, 1.0);

        v_Color = a_Color;
        v_Position = a_Position;
        v_Center = a_Center;
        v_Radius = a_Radius;
    }
)"