#ifndef RMCORE_LIGHT_H
#define RMCORE_LIGHT_H

#include "vector/vector3.h"

class rmcLightGroup {
public:
    Vector3 Ambient;
    Vector3 Pos[8];
    Vector3 Dir[8];
    Vector3 Color[8];
    float Intensity[8];
    // 0 = fx semantics (point: Intensity/dist^2 falloff; Intensity FLT_MAX = far point),
    // 1 = plain directional (Dir = direction the light travels, lvlLightData).
    int Mode[8];

    void Reset() {
        Ambient.Set(0.0f);
        for (int i = 0; i < 8; i++) {
            Pos[i].Set(0.0f);
            Dir[i].Set(0.0f);
            Color[i].Set(0.0f);
            Intensity[i] = 0.0f;
            Mode[i] = 0;
        }
    }

    void InitDirectionalFromPoint(const Vector3 &targetPos = Vector3(0.0f, 0.0f, 0.0f)) {
        for (int i = 0; i < 8; i++) {
            if (Mode[i] == 0 && Intensity[i] > 0.0f) {
                Vector3 d = targetPos - Pos[i];
                float len = d.Mag();
                if (len > 0.0001f) {
                    Dir[i] = d * (1.0f / len);
                } else {
                    Dir[i].Set(0.0f, -1.0f, 0.0f);
                }
                Mode[i] = 1;
            }
        }
    }
};

#endif // RMCORE_LIGHT_H
