# ToDos for embedded port
- remove ntp code (or undef) for now
- remove file io, replace with statically compiled configs
- move leapseconds to header
- isolate platform dependend functionality in one place
  - timers / tasks
  - clocks
  - io
- define interface for platform specific code that is as lean as possible

# Notes / ToDos when it finally compiles
- check if rtOpts in ptp_task() get fully initialized.

# simplyfied execution order
- main
  - TimingDomainSetup
  - ptpd startup
  - timing service setup
  - protocol (here is the main loop)