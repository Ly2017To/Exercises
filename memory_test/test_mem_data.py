import os

def decimal_to_binary(decimal):
    binary = format(decimal, '016b')
    return binary

if __name__ == "__main__":

    line_1 = "0000000000000001"
    line_2 = "0000001000000011"
    line_3 = "0000010000000101"
    line_4 = "0000011000000111"
    line_part = "000000000000000100000010000000110000010000000101"

    with open('data.dat',  'w') as dataFile:

        count_line = 0;

        while count_line < 1024:

            count = 0

            while count<3:
                dataFile.writelines(line_1+line_2+line_3+line_4)
                count = count + 1

            line_tmp = line_part
            count_str = decimal_to_binary(count_line) + "\n"
            line_tmp = line_tmp + count_str
            dataFile.writelines(line_tmp)

            count_line = count_line + 1

