times = list()
prefix = "times"

for (i in 1:100) {
	name = paste(prefix, i, sep="")
	times[[i]] = read.table(name, sep=" ")
}

matrices = list()
tmatrices = list()
for (i in 1:100) {
	matrices[[i]] = data.matrix(times[[i]])
	tmatrices[[i]] = t(matrices[[i]])
}

executions = list()
for (i in 1:100) {
	executions[[i]] = matrix(0, nrow=100, ncol=1000)
}
for (i in 1:100) {
	for (j in 1:100) {
		executions[[i]][j,] = matrices[[j]][i,]
	}
}

rnames = vector(length=100)
cnames = vector(length=1000)

for (i in 1:100) {
	rnames[i] = paste("exec", i, sep="")
}
for (i in 1:1000) {
	cnames[i] = paste("event", i, sep="")
}

for (i in 1:100) {
	rownames(executions[[i]]) = rnames
	colnames(executions[[i]]) = cnames
}

# draw a boxplot of the times of the 50 first events for the third execution on 
# the second input
boxplot(executions[[2]][3,1:50])
# Boxplot the times of the 50 first events for the first 5 executions of the 
# second input
boxplot(executions[[2]][1:5,1:50])

medTimes = matrix(0, nrow=100, ncol=1000)
for (i in 1:100) {
	for (j in 1:1000) {
		medTimes[i, j] = median(executions[[i]][,j])
	}
}

matplot(1:1000, t(medTimes), type='p', pch='.')

