### process the signal that stored in a wave file
### frequency analysis

from scipy import signal
import matplotlib.pyplot as plt
import numpy as np
import wave_file_analysis
from os.path import dirname, join as pjoin
from scipy.io import wavfile
from numpy.fft import fft, ifft, fftshift
import scipy as sig

def samples_plot(signal, y_label_string):
    plt.figure()
    plt.xlabel('Sample Index')
    plt.ylabel(y_label_string)
    plt.plot(np.arange(0, len(signal), 1), signal)
    plt.show()
    return

def time_domain_plot(signal, num_samples, fs, y_label_string):
    time_length = num_samples / fs
    time_select = np.linspace(0., time_length, num_samples)
    plt.figure()
    plt.xlabel('Time (seconds)')
    plt.ylabel(y_label_string)
    plt.plot(time_select, signal)
    plt.show()
    return

def time_domain_plot_twochannles(signal_channel_left, signal_channel_right, fs):
    num_samples = len(signal_channel_left)
    time_length = num_samples / fs
    time_select = np.linspace(0., time_length, num_samples)
    plt.figure()
    plt.xlabel('Time (seconds)')
    plt.ylabel('Double Channels Signal')
    plt.plot(time_select, signal_channel_left, label="Left channel")
    plt.plot(time_select, signal_channel_right, label="Right channel")
    plt.legend()
    plt.show()
    return

def frequency_domain_plot(signal, num_samples, fs, y_label_string):
    x = fftshift(fft(signal))
    delta_f = fs / num_samples
    freq = np.arange(-fs / 2, fs / 2, delta_f)
    plt.figure()
    plt.stem(freq, np.abs(x), 'b', markerfmt=" ", basefmt="-b")
    plt.xlabel('Freq (Hz)')
    plt.ylabel(y_label_string)
    plt.show()
    return

def filter_frequency_response_plot(w):
    length=1024
    h_padded = np.zeros(length)
    h_padded[0 : len(w)] = w
    h_f = np.abs(np.fft.fft(h_padded))[0: length // 2 + 1]

    # Plot frequency response (in dB) in normalized frequency.
    plt.figure()
    plt.plot(np.linspace(0, 0.5, len(h_f)), 20 * np.log10(h_f))
    plt.xlabel('Normalized frequency')
    plt.ylabel('Gain [dB]')
    plt.grid()
    plt.show()
    return

def twos_complement_to_int(val,num_bits):
    value = int(val,2)
    sign_bit = value >> (num_bits-1)
    if sign_bit & 1 :
        value = value - (1 << num_bits)
    return value

def int_to_twos_complement(num):
    # Convert the integer to binary
    binary = bin(num & 0xffff)  # Mask with 16 bits to handle negative numbers
    #binary = bin(num & 0xffffffff)  # Mask with 32 bits to handle negative numbers
    # Convert negative numbers to two's complement
    if num < 0:
        ### 16 bits
        binary = bin((1<<16)+num)
        # Remove the '0b' prefix
        binary = binary[2:]
        # Pad the binary string with leading zeros
        binary = binary.zfill(16)
        ### 32 bits
        # binary = bin((1 << 32) + num)
        # Remove the '0b' prefix
        # binary = binary[2:]
        # Pad the binary string with leading zeros
        #binary = binary.zfill(32)
        # Convert the binary string to hex

    #hex_str = hex(int(binary, 2))

    ###16 bits
    hex_str = format(int(binary, 2), '04x')
    ###32 bits
    #hex_str = format(int(binary, 2), '08x')

    return hex_str

def write_data_to_file_hex(data, file_name):
    data_arr = np.asarray(data)
    data_arr_hex = [];
    for x in data_arr:
            data_arr_hex.append(int_to_twos_complement(x))
    data_arr_hex = np.asarray(data_arr_hex)
    print(data_arr_hex)
    np.savetxt(file_name, data_arr_hex, fmt = '%s', delimiter=" ")

if __name__ == '__main__':
    ### wave file
    file = '.wav'

    ### read the wave file
    samplerate, data = wavfile.read(file)
    print(data.shape)
    print(f"samplerate = {samplerate}Hz")
    print(f"number of channels = {data.shape[1]}")
    data_channel_left = data[:, 0]
    data_channel_right = data[:, 1]

    ## plot the signal of two channels
    ## x-axis: time in seconds
    #time_domain_plot_twochannles(data_channel_left,data_channel_right,samplerate)

    ## plot with number of samples on x-axis
    #samples_plot(data_channel_left, 'data of the left channel')
    #samples_plot(data_channel_right, 'data of the right channel')

    ### select part of the signal with start_index and end_index
    start_index = 
    end_index = 
    num_points = end_index - start_index
    data_channel_a = data_channel_left[start_index:end_index]
    data_channel_b = data_channel_right[start_index:end_index]
    #time domain plot
    #time_domain_plot(data_channel_a,len(data_channel_a),samplerate,"data_channel_a")
    #time_domain_plot(data_channel_b,len(data_channel_b),samplerate,"data_channel_b")

    #Frequency domain plot
    #frequency_domain_plot(data_channel_a,num_points,samplerate,'FFT Amplitude |data_channel_a(freq)|')
    #frequency_domain_plot(data_channel_b,num_points,samplerate,'FFT Amplitude |data_channel_b(freq)|')

    ### coefficients of filter
    w=[];
    #print(len(w))
    #print(w)
    ### plot the frequency response of the filter in the normalized scale
    #filter_frequency_response_plot(w)

    data_channel_a_fir = np.convolve(data_channel_a,w)
    data_channel_b_fir = np.convolve(data_channel_b,w)

    #time_domain_plot(data_channel_a_fir,len(data_channel_a_fir),samplerate,"data_channel_a_fir")
    #time_domain_plot(data_channel_b_fir,len(data_channel_b_fir),samplerate,"data_channel_b_fir")

    #frequency_domain_plot(data_channel_a_fir,len(data_channel_a_fir),samplerate,'FFT Amplitude |data_channel_a_fir(freq)|')
    #frequency_domain_plot(data_channel_b_fir,len(data_channel_b_fir),samplerate,'FFT Amplitude |data_channel_b_fir(freq)|')

    ###write data to file
    write_data_to_file_hex(data_channel_a, 'data_channel_a.csv')
    write_data_to_file_hex(data_channel_b, 'data_channel_b.csv')


