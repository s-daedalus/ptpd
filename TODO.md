# ToDos for ptpd lwip port to freeRTOS tcp port
- the two receive callbacks get to be freeRTOS tasks witch block until data is received and then enqueue it
- all lwip specific calls in all files exept the ptpd_net files get wrappers into ptpd_net and then get replaced by their counterparts there
