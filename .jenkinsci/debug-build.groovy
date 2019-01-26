#!/usr/bin/env groovy

def doDebugBuild(coverageEnabled=false) {
  def dPullOrBuild = load ".jenkinsci/docker-pull-or-build.groovy"
  def manifest = load ".jenkinsci/docker-manifest.groovy"
  def pCommit = load ".jenkinsci/previous-commit.groovy"
  def parallelism = params.PARALLELISM
  def sanitizeEnabled = params.sanitize
  def fuzzingEnabled = params.fuzzing
  def platform = sh(script: 'uname -m', returnStdout: true).trim()
  def previousCommit = pCommit.previousCommitOrCurrent()
  // params are always null unless job is started
  // this is the case for the FIRST build only.
  // So just set this to same value as default.
  // This is a known bug. See https://issues.jenkins-ci.org/browse/JENKINS-41929


  sh("python analyze.py result.txt")


  def plots = []
  def files = sh(script: 'ls reports/*.csv', returnStdout: true).trim() //findFiles(glob: 'reports/*.csv')
  for(String el : files.split("\\r?\\n")) {
    // plots << [displayTableFїlag: true, exclusionValues: '', file: el, inclusionFlag: 'OFF']
  plot csvFileName: 'plot-3d136de2-a268-4abc-80a1-9f31db39b92d.csv', 
    csvSeries: plots, 
    group: 'iroha_build_time_graph', 
    numBuilds: '1', 
    style: 'line', 
    width: 4000,
    height: 3000,
    yaxisMinimum: '0',
    yaxisMaximum: '10',
    hasLegend: false,
    title: (Math.abs( new Random().nextInt() % (99 - 40) ) + 40).toString(), 
    useDescr: false,
    yaxis: 'Time, sec'
}
  }
//   println plots
//   plot csvFileName: 'plot-3d136de2-a268-4abc-80a1-9f31db39b92d.csv', 
//     csvSeries: plots, 
//     group: 'iroha_build_time_graph', 
//     numBuilds: '1', 
//     style: 'line', 
//     width: 4000,
//     height: 3000,
//     yaxisMinimum: '0',
//     yaxisMaximum: '10',
//     hasLegend: false,
//     title: 'Build time',
//     useDescr: false,
//     yaxis: 'Time, sec'
// }

return this
