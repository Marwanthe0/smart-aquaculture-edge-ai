#ifndef WATER_QUALITY_MODEL_H
#define WATER_QUALITY_MODEL_H

// Auto-generated TinyML Water Quality Classifier for ESP-32
// Author: Shafikul Islam Marwan
// Classes: 0 = OPTIMAL, 1 = WARNING, 2 = CRITICAL

inline int predict_water_quality(float temperature, float ph, float turbidity) {
    if (turbidity <= 75.0360f) {
        if (ph <= 9.4999f) {
            if (ph <= 5.5005f) {
                return 2; // Class 2
            } else {
                if (temperature <= 34.0274f) {
                    return 1; // Class 1
                } else {
                    return 2; // Class 2
                }
            }
        } else {
            return 2; // Class 2
        }
    } else {
        return 2; // Class 2
    }
}

inline const char* get_water_status_str(int status) {
    switch (status) {
        case 0: return "OPTIMAL";
        case 1: return "WARNING";
        case 2: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

#endif // WATER_QUALITY_MODEL_H
