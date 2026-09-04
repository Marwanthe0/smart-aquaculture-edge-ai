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
