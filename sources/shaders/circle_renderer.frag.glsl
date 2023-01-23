R"(
    layout(location = 0)in vec4 v_Color;
    layout(location = 1)in vec2 v_Position;
    layout(location = 2)in vec2 v_Center;
    layout(location = 3)in flat float v_Radius;

    layout(location = 0)out vec4 f_Color;

    void main(){

        if(length(v_Center) > v_Radius)
            discard;
        f_Color = v_Color;
    }
)"