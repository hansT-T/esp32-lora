'''

@Description : 因为arduino程序本身不具备存储能力，所以所有的信息均通过串口打印，该程序主要从串口信息中处理出对应的TxDoneTime
@author : william
@usage : 在程序对应的目录下创建test.txt，复制进去所有的串口信息，然后执行程序即可，实测这样最方便，不需要改动程序，然后复制出处理完成的时间到excel进行后续处理

'''


import re

f = open("test.txt","r+",encoding = 'utf-8')
dataf = open("TxDoneTime.txt","w+")
for s in f.readlines():
    l = re.findall("TxDoneTime:",s)
    if(len(l)>0):
        #print(l)
        start = s.find("TxDoneTime:")
        end = s.find("\n")
        #dataf.write("[")
        print(s[start+11:end])
        dataf.write(s[start+11:end])
        dataf.write("\n")