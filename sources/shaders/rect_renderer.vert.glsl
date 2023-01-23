R"(
    layout(location = 0)in vec2 a_Position;
    layout(location = 1)in vec2 a_TexCoords;
    layout(location = 2)in vec4 a_Color;
    layout(location = 3)in float a_TexIndex;

    layout(location = 0)out vec4 v_Color;
    layout(location = 1)out vec2 v_TexCoords;
    layout(location = 2)out flat float v_TexIndex;

    layout(std140, binding = 0)uniform MatricesUniform{
        mat4 u_Projection;
    };

    void main(){
        gl_Position = u_Projection * vec4(a_Position.xy, 0.0, 1.0);

        v_Color = a_Color;
        v_TexCoords = a_TexCoords;
        v_TexIndex = a_TexIndex;
    }
)"