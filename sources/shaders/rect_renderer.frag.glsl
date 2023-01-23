R"(
    layout(location = 0)in vec4 v_Color;
    layout(location = 1)in vec2 v_TexCoords;
    layout(location = 2)in flat float v_TexIndex;

    layout(location = 0)out vec4 f_Color;

    layout(binding = 1)uniform sampler2D u_Textures[15];

    void main(){
        f_Color = v_Color * texture(u_Textures[int(v_TexIndex)], v_TexCoords);
    }
)"