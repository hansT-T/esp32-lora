from sklearn import linear_model
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


'''

@Description : 该程序为线性回归函数，用于拟合两个时间之间的关系，即Tb = skew * Ta + offset
@author : william
@usage : 需使用处理完成后的数据（提取/对齐），存储在testnode.csv中，作为两列Ta与Tb，然后执行即可，使用滑动窗口计算skew和offset

'''



trainset = pd.read_csv("testnode.csv")
X = trainset['Ta']
Y = trainset['Tb']
X = np.array(X).reshape(-1, 1)
Y = np.array(Y).reshape(-1, 1)
i = 0

skew = open("skew.txt","w+")
offset  = open("offset.txt","w+")

# 此处滑动窗口大小采用30
while i+30<=len(X):
    dataX=X[i:i+30]
    dataY=Y[i:i+30]
    regr = linear_model.LinearRegression(fit_intercept=True)

    regr.fit(dataX, dataY)
    
    print('skew: \n', regr.coef_)
    print('offset:',regr.intercept_)
    s = str(regr.coef_[0])
    skew.write(s[1:len(s)-1])
    skew.write("\n")
    s = str(regr.intercept_)
    offset.write(s[1:len(s)-1])
    offset.write("\n")
    i=i+1