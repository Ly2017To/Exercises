###reference link:https://blog.csdn.net/gemengxia/article/details/112475073
### apply cordic algorithm to calculate magnitude and phase of a vector
### formula:
# x(k+1) = x(k)*-y(k)*d(k)*2^(-k))
# y(k+1) = y(k)*+x(k)*d(k)*2^(-k))
# z(k+1) = z(k)-d(k)*arctan(2^(-k))
# counter-clockwise d(k)=1; clockwise d(k)=-1;

#dependencies
import math
import numpy as np
import matplotlib.pyplot as plt

# close all previous plots
plt.close('all')

# constant
PI = math.pi

# convert degree to radius
def degree_to_radius(degree):
    return (degree / 180) * PI

# convert radius to degree
def radius_to_degree(radius):
    return (radius / PI) * 180

def plot_signals_I_Q(signal_I, signal_Q, t):

    plt.figure()
    plt.plot(t, signal_I, color='r', label='I channel')
    plt.plot(t, signal_Q, color='b', label='Q channel')

    # Naming the x-axis, y-axis and the whole graph
    plt.xlabel("Time(s)")
    plt.ylabel("Magnitude")
    plt.title("I and Q Channels Signal Plot")

    # Adding legend, which helps us recognize the curve according to it's color
    plt.legend()
    # To load the display window
    plt.show()

    return

def plot_signals(title, signal_a, signal_a_label, signal_b, signal_b_label, t):

    plt.figure()
    plt.plot(t, signal_a, color='r', label=signal_a_label)
    plt.plot(t, signal_b, color='b', label=signal_b_label)

    # Naming the x-axis, y-axis and the whole graph
    plt.xlabel("Time(s)")
    plt.title(title)

    # Adding legend, which helps us recognize the curve according to it's color
    plt.legend()
    # To load the display window
    plt.show()

    return

if __name__ == '__main__':
    # number of iterations
    N = 16
    K = np.arange(N)
    # scale factor
    scale_factor = np.power(2, 16)
    # calculate the tangent value
    tan_value = np.power(0.5,K)
    # calculate the phase of the corresponding tangent value in radius
    pha_value = np.arctan(tan_value)
    # print(pha_value)
    pha_value = np.arctan(tan_value) * scale_factor
    pha_value = np.rint(pha_value)
    print(pha_value)
    # print("maximum absolute value of angle can be calculated",(np.sum(pha_value)/PI)*180)

    # multiply cosine value of the phase at each iteration
    cos_value = np.sqrt(1/(np.square(tan_value)+1))
    # print(cos_value)
    cos_product = np.prod(cos_value)
    # print(cos_product)

    # use two different channels to test
    # unit circle
    t = np.linspace(0,1,num=101)
    signal_I = np.cos(2*PI*1*t)
    signal_Q = np.sin(2*PI*1*t)

    plot_signals_I_Q(signal_I, signal_Q, t)

    magnitude = np.zeros(len(t))
    phase = np.zeros(len(t))
    phase_degree = np.zeros(len(t))

    for i in range(len(t)):
        x_in = signal_I[i]
        y_in = signal_Q[i]
        ### calculate phase shift based on coordinate [-PI,PI]
        # first quadrant and fourth quadrant
        phase_shift = 0
        x = x_in
        y = y_in
        # second quadrant
        if x_in < 0 and y_in > 0:
            phase_shift = np.rint(PI/2*scale_factor)
            x = y_in
            y = -x_in
        # third quadrant
        if x_in < 0 and y_in < 0:
            phase_shift = -np.rint(PI/2*scale_factor)
            x = -y_in
            y = x_in

        '''    
        ### calculate phase shift based on coordinate [0,2PI]
        # first quadrant
        phase_shift = 0
        x = x_in
        y = y_in
        # second quadrant
        if x_in < 0 and y_in > 0:
            phase_shift = PI / 2
            x = y_in
            y = -x_in
        # third quadrant
        if x_in < 0 and y_in < 0:
            phase_shift = PI
            x = -x_in
            y = -y_in
        # fourth quadrant
        if x_in > 0 and y_in < 0:
            phase_shift = 1.5 * PI
            x = -y_in
            y = x_in
        '''

        ### iteration
        for k in K:
            x_tmp = x
            if y > 0:
                x = x_tmp + y * tan_value[k]
                y = y - x_tmp * tan_value[k]
                phase[i] = phase[i] + pha_value[k]
            else:
                x = x_tmp - y * tan_value[k]
                y = y + x_tmp * tan_value[k]
                phase[i] = phase[i] - pha_value[k]

        magnitude[i] = x*cos_product
        phase[i] = (phase[i] + phase_shift)/scale_factor
        phase_degree[i] = phase[i] * 180/PI

    plot_signals("Magnitude and Phase Plot",magnitude,"Magnitude",phase,"Phase(radius)",t)



