#ifndef WATER_QUALITY_MODEL_H
#define WATER_QUALITY_MODEL_H

// Auto-generated TinyML Water Quality Classifier for ESP-32
// Author: Shafikul Islam Marwan
// Classes: 0 = OPTIMAL, 1 = WARNING, 2 = CRITICAL

inline int predict_water_quality(float temperature, float ph, float turbidity) {
    if (turbidity <= 34.5673f) {
        if (ph <= 8.5054f) {
            if (ph <= 6.5128f) {
                if (ph <= 5.4915f) {
                    return 2; // Class 2
                } else {
                    if (temperature <= 34.0519f) {
                        if (temperature <= 17.9361f) {
                            return 2; // Class 2
                        } else {
                            return 1; // Class 1
                        }
                    } else {
                        return 2; // Class 2
                    }
                }
            } else {
                if (temperature <= 21.9040f) {
                    if (temperature <= 17.9326f) {
                        return 2; // Class 2
                    } else {
                        if (temperature <= 18.1045f) {
                            return 1; // Class 1
                        } else {
                            return 1; // Class 1
                        }
                    }
                } else {
                    if (temperature <= 31.0705f) {
                        return 0; // Class 0
                    } else {
                        if (temperature <= 33.9867f) {
                            return 1; // Class 1
                        } else {
                            return 2; // Class 2
                        }
                    }
                }
            }
        } else {
            if (ph <= 9.4990f) {
                if (temperature <= 34.1570f) {
                    if (temperature <= 18.0062f) {
                        return 2; // Class 2
                    } else {
                        if (ph <= 8.5201f) {
                            return 1; // Class 1
                        } else {
                            return 1; // Class 1
                        }
                    }
                } else {
                    return 2; // Class 2
                }
            } else {
                if (ph <= 9.5183f) {
                    return 2; // Class 2
                } else {
                    return 2; // Class 2
                }
            }
        }
    } else {
        if (turbidity <= 74.9392f) {
            if (ph <= 5.5245f) {
                return 2; // Class 2
            } else {
                if (ph <= 9.5204f) {
                    if (temperature <= 34.0045f) {
                        if (temperature <= 18.0015f) {
                            return 2; // Class 2
                        } else {
                            return 1; // Class 1
                        }
                    } else {
                        if (ph <= 5.6166f) {
                            return 2; // Class 2
                        } else {
                            return 2; // Class 2
                        }
                    }
                } else {
                    if (ph <= 9.5468f) {
                        return 2; // Class 2
                    } else {
                        return 2; // Class 2
                    }
                }
            }
        } else {
            return 2; // Class 2
        }
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
