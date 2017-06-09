def computeMeans(filename):
    meansFile = open(filename, 'r')
    meansRuns = meansFile.readlines()
    meansTimes = []
    for meansRun in meansRuns:
        total = 0
        meansTimesStr = meansRun.split()
        for meansTimeStr in meansTimesStr:
            total += int(meansTimeStr)
            #print int(meansTimeStr)
        #print(len(meansTimesStr))
        meansTimes.append(total / len(meansTimesStr))

    return meansTimes

meansTimes = computeMeans("meansTime")
print "Means of means: "
print meansTimes

meansMins = computeMeans("minTimes")
print "Means of mins: "
print meansMins

meansMax = computeMeans("maxTimes")
print "Means of max: "
print meansMax

