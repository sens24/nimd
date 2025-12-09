q = "uwu"
#q = "a"*72
print(len(q))
suf = "OPEN|"+q+"|"
pre = "0|"+("0"*(len(str(len(suf)))==1))+str(len(suf))+"|"
print(pre+suf)