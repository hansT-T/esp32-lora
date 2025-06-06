import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import time
from sklearn import svm
from sklearn import model_selection
import lightgbm as lgb
import seaborn as sns
from sklearn.metrics import confusion_matrix
from matplotlib.colors import LinearSegmentedColormap

from sklearn.preprocessing import StandardScaler
def plot_confusion_matrix(cm, savename,classes, title='Confusion Matrix'): #绘制混淆矩阵
    FP = sum(cm.sum(axis=0)) - sum(np.diag(cm)) #假正样本数 
    FN = sum(cm.sum(axis=1)) - sum(np.diag(cm)) #假负样本数
    TP = sum(np.diag(cm)) #真正样本数
    TN = sum(cm.sum().flatten()) - (FP + FN + TP) #真负样本数
    SUM = TP+FP
    PRECISION = TP / (TP+FP)  # 查准率，又名准确率
    RECALL = TP / (TP+FN)  # 查全率，又名召回率
    plt.figure(figsize=(12, 8), dpi=100)
    np.set_printoptions(precision=2)
    # 在混淆矩阵中每格的概率值
    ind_array = np.arange(len(classes)+1)
    x, y = np.meshgrid(ind_array, ind_array)#生成坐标矩阵
    diags = np.diag(cm)#对角TP值
    TP_FNs, TP_FPs = [], []
    for x_val, y_val in zip(x.flatten(), y.flatten()):#并行遍历
        max_index = len(classes)
        if x_val != max_index and y_val != max_index:#绘制混淆矩阵各格数值
            c = cm[y_val][x_val]
            plt.text(x_val, y_val, c, color='black', fontsize=15, va='center', ha='center',fontproperties='Times New Roman')
        elif x_val == max_index and y_val != max_index:#绘制最右列即各数据类别的查全率
            TP = diags[y_val]
            TP_FN = cm.sum(axis=1)[y_val]
            recall = TP / (TP_FN)
            if recall != 0.0 and recall > 0.01:
                recall = str('%.2f'%(recall*100,))
            elif recall == 0.0:
                recall = '0'
            TP_FNs.append(TP_FN)
            plt.text(x_val, y_val, str(TP_FN)+'\n'+str(recall)+'%', color='black', va='center', ha='center',fontproperties='Times New Roman')
        elif x_val != max_index and y_val == max_index:#绘制最下行即各数据类别的查准率
            TP = diags[x_val]
            TP_FP = cm.sum(axis=0)[x_val]
            precision = TP / (TP_FP)
            if precision != 0.0 and precision > 0.01:
                precision = str('%.2f'%(precision*100,))+'%'
            elif precision == 0.0:
                precision = '0'
            TP_FPs.append(TP_FP)
            plt.text(x_val, y_val, str(TP_FP)+'\n'+str(precision), color='black', va='center', ha='center',fontproperties='Times New Roman')
    cm = np.insert(cm,max_index,TP_FNs,1)
    cm = np.insert(cm,max_index,np.append(TP_FPs,SUM),0)
    plt.text(max_index, max_index, str(SUM)+'\n'+str('%.2f'%(PRECISION*100,))+'%', color='red', va='center', ha='center',fontproperties='Times New Roman')
    plt.imshow(cm, interpolation='nearest', cmap=plt.cm.YlGn)
    #plt.title(title)
    plt.colorbar()
    xlocations = np.array(range(len(classes)))
    plt.xticks(xlocations, classes, rotation=90,fontproperties='Times New Roman')
    plt.yticks(xlocations, classes,fontproperties='Times New Roman')
    plt.ylabel('True Label',fontproperties='Times New Roman')
    plt.xlabel('Predict Label',fontproperties='Times New Roman')
    # offset the tick
    tick_marks = np.array(range(len(classes))) + 0.5
    plt.gca().set_xticks(tick_marks, minor=True)
    plt.gca().set_yticks(tick_marks, minor=True)
    plt.gca().xaxis.set_ticks_position('none')
    plt.gca().yaxis.set_ticks_position('none')
    plt.grid(True, which='minor', linestyle='-')
    #plt.gcf().subplots_adjust(bottom=0.15)
    # show confusion matrix
    plt.savefig(savename, format='pdf')
    plt.show()



trainset =  pd.read_csv("./final/s1-delta.csv")
Train_X = trainset.drop(['num','id','time'], axis=1)
Train_Y = trainset['id']
Trian_X = np.array(Train_X).reshape(-1, 1)

testset =  pd.read_csv("./final/s4-delta.csv")
test_X = testset.drop(['num','id','time'], axis=1)
test_Y = testset['id']
test_X = np.array(test_X).reshape(-1, 1)





#d_train=lgb.Dataset(X_train, label=y_train)
X_train, X_test, y_train, y_test = Train_X,test_X,Train_Y,test_Y


'''
trainset = pd.read_csv("./final/2025-526.csv")
Train_X = trainset.drop(['num','id'], axis=1)
Train_Y = trainset['id']
X_train, X_test, y_train, y_test = model_selection.train_test_split(Train_X, Train_Y, test_size = 0.3
                                                   ,random_state = 2024,stratify=Train_Y)
'''
from sklearn.neighbors import KNeighborsClassifier
start_time = time.time()
neigh = KNeighborsClassifier(n_neighbors=10)
X = X_train.values
Y=y_train.values
neigh.fit(X,Y)
y_pred=neigh.predict(X_test)

print(time.time() - start_time)
cm = confusion_matrix(y_test, y_pred, labels=None, sample_weight=None)
classse = ['1','2','3','4','5','6','7','8','9','10']
#classse = ['EN6','EN7','EN8','EN9','EN10']
#sns.heatmap(cm,annot=True,fmt='d',cmap='YlGn',cbar=False,xticklabels=classse,yticklabels=classse)
#sns.heatmap(cm,annot=True,fmt='d',cmap='YlOrBr',cbar=False,xticklabels=classse,yticklabels=classse)
#new_blues=sns.color_palette("OrRd", 10)[0:10]
#new_blues=sns.color_palette("YlOrBr", 10)

'''
colors = [
    (247/255, 247/255, 231/255),  # 浅黄色起点 (247,247,231)
    (138/255, 88/255, 88/255)     # 深棕色终点 (138,88,88)
]

# 创建自定义colormap对象
new_blues = LinearSegmentedColormap.from_list("CustomColormap", colors, N=256)
sns.heatmap(cm,annot=True,fmt='d',cmap=new_blues,cbar=False,xticklabels=classse,yticklabels=classse)
'''

plt.xlabel('预测标签', fontdict={'family': 'SimSun', 'size': 18})
plt.ylabel('真实标签', fontdict={'family': 'SimSun', 'size': 18})
#plt.xlabel('Predicted label',font={'family':'Times New Roman','size':18})
#plt.ylabel('True label',font={'family':'Times New Roman','size':18})
plt.savefig('knn'+'.pdf', format='pdf')
#plot_confusion_matrix(cm, 'cm_'+'.pdf', title='confusion matrix ',classes = classse)