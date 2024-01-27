import sqlite3
import json
import xlsxwriter
import re
import matplotlib.pyplot as plt
import numpy as np

# to do the label of plot
def autolabel(rects):
    for rect in rects:
        height = rect.get_height()
        plt.text(rect.get_x()+rect.get_width()/2.- 0.2, 1.03*height, '%s' % int(height))

# to establish a connection to database use connect method
con = sqlite3.connect('./filename.db')
# to create a cursor object to execute sqlite queries or commands
cur = con.cursor()

# to select table from database
select_table_statement = '''SELECT * FROM table_name'''
cur.execute(select_table_statement)
# to read query result
output = cur.fetchall()

workbook = xlsxwriter.Workbook('./workbook.xlsx')
worksheet = workbook.add_worksheet()

row = 0
col = 0

worksheet.write(row, col, "first_colomn_name")
worksheet.write(row, col + 1, "second_colomn_name")
### to write other information

row = row + 1

statistics_list = [] 

for line in output:
    #extract the json data from table which is at index
    if line[index]!= 'NULL':
        data = json.loads(line[index])
        #print(data)

        variable = data['variable_name']
        #print(variable)

        float_variable_string = data['float_variable_string_name']
        float_variable = float((re.findall(r"[-+]?\d*\.\d+|\d+", float_variable_string))[0])
        #print(float_variable_string) example ,"speed":"100.0 km/h",

        numbers_array = data['numbers_array_name']
   
        for tag in tags_info:
            number = re.findall('[0-9]+', tag)

        ### to add code here to process the data
        ### to add code here to get the statistics of data
           
        worksheet.write_string(row, col , str(variable), None)
        worksheet.write(row, col + 1, float_variable)
		### to write other information

        row=row+1

### label of x axis
### for example x_label_list = ['0-10', '10-20', '20-30']
x_label_list = []

autolabel(plt.bar(range(len(statistics_list)), statistics_list, color='b', tick_label=x_label_list))
plt.xlabel("x_label_list_name", fontsize=20)
plt.ylabel("statistics_list_name", fontsize=20)
plt.title("title of the plot", fontsize=15)

plt.show()

# close to write the excel file
workbook.close()

# close the cursor
cur.close()

# close the connection
con.close()
