times = list()
prefix = "times"
outputPrefix=basename(getwd())
niter=100
ninputs=100
nevents=1000

for (i in 1:niter) {
	name = paste(prefix, i, sep="")
	times[[i]] = read.table(name, sep=" ")
}

matrices = list()
tmatrices = list()
for (i in 1:niter) {
	matrices[[i]] = data.matrix(times[[i]])
	tmatrices[[i]] = t(matrices[[i]])
}

executions = list()
for (i in 1:ninputs) {
	executions[[i]] = matrix(0, nrow=niter, ncol=nevents)
}
for (i in 1:ninputs) {
	for (j in 1:niter) {
		executions[[i]][j,] = matrices[[j]][i,]
	}
}

rnames = vector(length=niter)
cnames = vector(length=nevents)

for (i in 1:niter) {
	rnames[i] = paste("exec", i, sep="")
}
for (i in 1:nevents) {
	cnames[i] = paste("event", i, sep="")
}

for (i in 1:ninputs) {
	rownames(executions[[i]]) = rnames
	colnames(executions[[i]]) = cnames
}

medTimes = matrix(0, nrow=ninputs, ncol=nevents)
for (i in 1:ninputs) {
	for (j in 1:nevents) {
		medTimes[i, j] = median(executions[[i]][,j])
	}
}

pdf(paste(outputPrefix, "scatter_meds.pdf", sep="_"))
matplot(1:1000, t(medTimes), type="p", pch='.', xlab="", ylab="")
dev.off()

pdf(paste(outputPrefix, "scatter_lmeds.pdf", sep="_"))
matplot(1:1000, log10(t(medTimes)), type='p', pch='.', xlab="", ylab="")
dev.off()

