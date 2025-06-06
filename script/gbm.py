import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn import svm
from sklearn import model_selection
import lightgbm as lgb
import seaborn as sns
from sklearn.metrics import confusion_matrix
from sklearn.preprocessing import StandardScaler
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from matplotlib.colors import LinearSegmentedColormap

import numpy as np
import matplotlib.pyplot as plt


#绘制混淆矩阵，忽略

def plot_confusion_matrix(cm, savename, classes, title='Confusion Matrix'):
    FP = sum(cm.sum(axis=0)) - sum(np.diag(cm))  # 假正样本数 
    FN = sum(cm.sum(axis=1)) - sum(np.diag(cm))  # 假负样本数
    TP = sum(np.diag(cm))  # 真正样本数
    TN = sum(cm.sum().flatten()) - (FP + FN + TP)  # 真负样本数
    SUM = TP + FP
    PRECISION = TP / (TP + FP)  # 查准率，又名准确率
    RECALL = TP / (TP + FN)  # 查全率，又名召回率
    print(PRECISION)
    print(RECALL)
    # 归一化处理，按照每个类别自己的比例计算颜色深浅
    cm_normalized = cm.astype('float') / cm.sum(axis=1, keepdims=True)
    cm_normalized[np.isnan(cm_normalized)] = 0  # 避免零除引发的 NaN

    plt.figure(figsize=(12, 8), dpi=100)
    np.set_printoptions(precision=2)

    ind_array = np.arange(len(classes) + 1)
    x, y = np.meshgrid(ind_array, ind_array)  # 生成坐标矩阵
    diags = np.diag(cm)  # 对角 TP 值

    TP_FNs, TP_FPs = [], []
    for x_val, y_val in zip(x.flatten(), y.flatten()):  # 并行遍历
        max_index = len(classes)
        if x_val != max_index and y_val != max_index:  # 绘制混淆矩阵各格数值
            c = cm[y_val][x_val]
            plt.text(x_val, y_val, c, color='black', fontsize=15, va='center', ha='center', fontproperties='Times New Roman')
        elif x_val == max_index and y_val != max_index:  # 绘制最右列即各数据类别的查全率
            TP = diags[y_val]
            TP_FN = cm.sum(axis=1)[y_val]
            recall = TP / TP_FN if TP_FN > 0 else 0
            recall_text = str(f'{recall * 100:.2f}') + '%'
            TP_FNs.append(TP_FN)
            plt.text(x_val, y_val, f'{TP_FN}\n{recall_text}', color='black', va='center', ha='center', fontproperties='Times New Roman')
        elif x_val != max_index and y_val == max_index:  # 绘制最下行即各数据类别的查准率
            TP = diags[x_val]
            TP_FP = cm.sum(axis=0)[x_val]
            precision = TP / TP_FP if TP_FP > 0 else 0
            precision_text = str(f'{precision * 100:.2f}') + '%'
            TP_FPs.append(TP_FP)
            plt.text(x_val, y_val, f'{TP_FP}\n{precision_text}', color='black', va='center', ha='center', fontproperties='Times New Roman')

    cm_with_totals = np.insert(cm, max_index, TP_FNs, axis=1)
    cm_with_totals = np.insert(cm_with_totals, max_index, np.append(TP_FPs, SUM), axis=0)

    # 最后一列和最后一行的颜色深浅单独归一化
    row_sums = cm.sum(axis=1)
    col_sums = cm.sum(axis=0)
    row_sums_normalized = row_sums / row_sums.max()
    col_sums_normalized = col_sums / col_sums.max()

    cm_normalized_with_totals = cm_with_totals.astype('float')
    for i in range(len(classes)):
        cm_normalized_with_totals[i, -1] = row_sums_normalized[i]
        cm_normalized_with_totals[-1, i] = col_sums_normalized[i]

    cm_normalized_with_totals[-1, -1] = cm_with_totals[-1, -1] / cm_with_totals[-1, -1].max()

    plt.text(max_index, max_index, f'{SUM}\n{PRECISION * 100:.2f}%', color='red', va='center', ha='center', fontproperties='Times New Roman')

    # 使用归一化的混淆矩阵绘制颜色深浅
    plt.imshow(cm_normalized_with_totals, interpolation='nearest', cmap=plt.cm.YlGn)
    plt.colorbar()

    xlocations = np.array(range(len(classes) + 1))
    plt.xticks(xlocations[:-1], classes, rotation=90, fontproperties='Times New Roman')
    plt.yticks(xlocations[:-1], classes, fontproperties='Times New Roman')
    plt.ylabel('True Label', fontproperties='Times New Roman')
    plt.xlabel('Predict Label', fontproperties='Times New Roman')

    tick_marks = np.array(range(len(classes) + 1)) + 0.5
    plt.gca().set_xticks(tick_marks, minor=True)
    plt.gca().set_yticks(tick_marks, minor=True)
    plt.gca().xaxis.set_ticks_position('none')
    plt.gca().yaxis.set_ticks_position('none')
    plt.grid(True, which='minor', linestyle='-')

    plt.savefig(savename, format='pdf')
    plt.show()


#单独csv文件作为训练集和测试集

#'''
trainset =  pd.read_csv("./final/s1-delta.csv")
Train_X = trainset.drop(['num','id'], axis=1)
Train_Y = trainset['id']
Trian_X = np.array(Train_X).reshape(-1, 2)

testset = pd.read_csv("./final/s4-delta.csv")
test_X = testset.drop(['num','id'], axis=1)
test_Y = testset['id']
test_X = np.array(test_X).reshape(-1, 2)
X_train, X_test, y_train, y_test = Train_X,test_X,Train_Y,test_Y
#'''

#从同一个csv文件中划分训练集和测试集


'''
trainset = pd.read_csv("./final/2025-526.csv")
Train_X = trainset.drop(['num','id'], axis=1)

Train_Y = trainset['id']

X_train, X_test, y_train, y_test = model_selection.train_test_split(Train_X, Train_Y, test_size = 0.3
                                                   ,random_state = 2020,stratify=Train_Y)
'''

#gbm定义

d_train=lgb.Dataset(X_train, label=y_train)
params={}
params['learning_rate']=0.05
params['boosting_type']='gbdt' #GradientBoostingDecisionTree
params['objective']='multiclass' #Multi-class target feature
params['metric']='multi_logloss' #metric for multi-class
params['max_depth']=20
params['num_leaves'] = 256
params['num_class']=10 #no.of unique values in the target class not inclusive of the end value

clf=lgb.train(params,d_train,1000)
y_pred=clf.predict(X_test)
y_pred = np.argmax(y_pred, axis=1)

print(f"Number of trees trained: {clf.num_trees()}")

#绘制热力图，混淆矩阵的另一种

cm = confusion_matrix(y_test, y_pred, labels=None, sample_weight=None)
#classse = ['EN1','EN2','EN3','EN4','EN5','EN6','EN7','EN8','EN9','EN10']
classse = ['1','2','3','4','5','6','7','8','9','10']
#classse = ['EN6','EN7','EN8','EN9','EN10']
sns.heatmap(cm,annot=True,fmt='d',cmap='YlGn',cbar=False,xticklabels=classse,yticklabels=classse)
#sns.heatmap(cm,annot=True,fmt='d',cmap='YlOrBr',cbar=False,xticklabels=classse,yticklabels=classse)
#new_blues=sns.color_palette("OrRd", 10)[0:4]
#new_blues=sns.color_palette("YlOrRd", 10).reverse()

'''
colors = [
    (247/255, 247/255, 231/255),  # 浅黄色起点 (247,247,231)
    (138/255, 88/255, 88/255)     # 深棕色终点 (138,88,88)
]

# 创建自定义colormap对象
new_blues = LinearSegmentedColormap.from_list("CustomColormap", colors, N=256)
'''

#sns.heatmap(cm,annot=True,fmt='d',cmap=new_blues,cbar=False,xticklabels=classse,yticklabels=classse)
#plt.xlabel('Predicted label',font={'family':'Times New Roman','size':18})
#plt.ylabel('True label',font={'family':'Times New Roman','size':18})
plt.xlabel('预测标签', fontdict={'family': 'SimSun', 'size': 18})
plt.ylabel('真实标签', fontdict={'family': 'SimSun', 'size': 18})
plt.savefig('gbm'+'.pdf', format='pdf')
#plot_confusion_matrix(cm, 'cm_'+'.pdf', title='confusion matrix ',classes = classse)