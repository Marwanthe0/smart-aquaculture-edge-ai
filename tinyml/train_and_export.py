import os

import numpy as np
from sklearn.tree import DecisionTreeClassifier

# Seed for reproducible data distribution
np.random.seed(42)
num_samples = 1500

# Sensor range bounds based on standard freshwater aquaculture conditions
# Temperature in Celsius (typical range: 15.0 to 38.0 C)
temperature = np.random.uniform(15.0, 38.0, num_samples)

# Water acidity level (typical range: 4.0 to 11.0 pH)
ph = np.random.uniform(4.0, 11.0, num_samples)

# Turbidity measured in Nephelometric Turbidity Units (5.0 to 120.0 NTU)
turbidity = np.random.uniform(5.0, 120.0, num_samples)

# Class definitions:
# 0: OPTIMAL (Safe metabolic range for common freshwater species)
# 1: WARNING (Sub-optimal range, requires monitoring)
# 2: CRITICAL (Dangerous drift, requires immediate automated aeration)
labels = []
for t, p, tb in zip(temperature, ph, turbidity):
    if (22.0 <= t <= 31.0) and (6.5 <= p <= 8.5) and (tb <= 35.0):
        labels.append(0)
    elif (p < 5.5 or p > 9.5) or (tb > 75.0) or (t < 18.0 or t > 34.0):
        labels.append(2)
    else:
        labels.append(1)

# Feature matrix and ground truth target
X = np.column_stack([temperature, ph, turbidity])
y = np.array(labels)

print(f"Generated {num_samples} training samples across 3 sensor features.")


# Train Decision Tree Classifier
# max_depth=4 keeps execution fast and prevents tree over-expansion for microcontroller targets
classifier = DecisionTreeClassifier(max_depth=4, random_state=42)
classifier.fit(X, y)

accuracy = classifier.score(X, y) * 100.0
print(f"Model trained successfully. Accuracy: {accuracy:.2f}%")

#python to cpp generation
def export_tree_to_cpp(tree, feature_names):
    left = tree.tree_.children_left
    right = tree.tree_.children_right
    threshold = tree.tree_.threshold
    features = [feature_names[i] for i in tree.tree_.feature]
    values = tree.tree_.value

    def recurse(node, depth):
        indent = "    " * depth
        if left[node] != right[node]:
            var_name = features[node]
            thresh_val = threshold[node]
            return (
                f"{indent}if ({var_name} <= {thresh_val:.4f}f) {{\n"
                f"{recurse(left[node], depth + 1)}"
                f"{indent}}} else {{\n"
                f"{recurse(right[node], depth + 1)}"
                f"{indent}}}\n"
            )
        else:
            predicted_class = int(np.argmax(values[node]))
            return f"{indent}return {predicted_class}; // Class {predicted_class}\n"

    cpp_code = f"""#ifndef WATER_QUALITY_MODEL_H
#define WATER_QUALITY_MODEL_H

// Auto-generated TinyML Water Quality Classifier for ESP-32
// Author: Shafikul Islam Marwan
// Classes: 0 = OPTIMAL, 1 = WARNING, 2 = CRITICAL

inline int predict_water_quality(float temperature, float ph, float turbidity) {{
{recurse(0, 1)}}}

inline const char* get_water_status_str(int status) {{
    switch (status) {{
        case 0: return "OPTIMAL";
        case 1: return "WARNING";
        case 2: return "CRITICAL";
        default: return "UNKNOWN";
    }}
}}

#endif // WATER_QUALITY_MODEL_H
"""
    return cpp_code


# Generate C++ header content
header_content = export_tree_to_cpp(classifier, ["temperature", "ph", "turbidity"])

# Save to local tinyml directory
local_header_path = os.path.join(os.path.dirname(__file__), "water_quality_model.h")
with open(local_header_path, "w") as f:
    f.write(header_content)
print(f"Generated C++ model at: {local_header_path}")

# Copy into esp32/esp_code firmware folder so Arduino sketch can include it directly
firmware_header_path = os.path.join(
    os.path.dirname(__file__), "..", "esp32", "esp_code", "water_quality_model.h"
)
with open(firmware_header_path, "w") as f:
    f.write(header_content)
print(f"Linked C++ model to firmware: {firmware_header_path}")