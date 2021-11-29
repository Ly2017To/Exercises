#reference links:
#https://scikit-learn.org/stable/auto_examples/linear_model/plot_polynomial_interpolation.html
#https://scikit-learn.org/dev/modules/linear_model.html#polynomial-regression-extending-linear-models-with-basis-functions

import numpy as np
import matplotlib.pyplot as plt

from sklearn.linear_model import Ridge
from sklearn.preprocessing import PolynomialFeatures
from sklearn.pipeline import make_pipeline

def f(x):
    #Function to be approximated by polynomial interpolation
    #a second order polynomial as an example
    return x*x + 2*x - 20

# whole range we want to plot
x_plot = np.linspace(-10, 10, 100)

# the range of variable to make features
x_train = np.linspace(-10, 10, 100)
# randomly select 20 number of training data
rng = np.random.RandomState(0)
x_train = np.sort(rng.choice(x_train, size=20, replace=False))
y_train = f(x_train)

# create 2D-array versions of these arrays to feed to transformers
X_train = x_train[:, np.newaxis]
X_plot = x_plot[:, np.newaxis]

# plot function
lw = 2
fig, ax = plt.subplots()
ax.set_prop_cycle(
    color=["black", "teal"]
)
ax.plot(x_plot, f(x_plot), linewidth=lw, label="ground truth")

# plot training points
ax.scatter(x_train, y_train, label="training points")

# polynomial features
# change the order of degree here
for degree in [2]:
    model = make_pipeline(PolynomialFeatures(degree), Ridge(alpha=1e-3))
    model.fit(X_train, y_train)
    #
    y_plot = model.predict(X_plot)
    ax.plot(x_plot, y_plot, label=f"degree {degree}")

ax.legend(loc="lower center")
ax.set_ylim(-100, 150)
plt.show()