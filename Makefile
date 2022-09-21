# Note: type 'make Q=' to get make commands debug 
Q=@
PLUGIN_TASK_CONF='{"name": "s2opcua_server","plugin": "s2opcua","type": "north","schedule_type": 3,"schedule_day": 0,"schedule_time": 0,"schedule_repeat": 10,"schedule_enabled": true}'
PLUGIN_SERV_CONF='{"config":{}, "enabled" :"true", "name":"s2opcua_service", "plugin":"s2opcua", "type":"north"}'
CPPLINT_EXCLUDE='-build/include_subdir,-build/c++11'

all: build install_plugin # insert_task
build:
	$(Q)mkdir -p build
	$(Q)cd build && cmake -DCMAKE_BUILD_TYPE=Debug -DFLEDGE_INSTALL=$(FLEDGE_INSTALL) ..
	$(Q)make -C build -j4
unit_tests: 
	$(Q)mkdir -p build
	$(Q)cd build && cmake -DCMAKE_BUILD_TYPE=Coverage -DFLEDGE_INSTALL=$(FLEDGE_INSTALL) ..
	$(Q)make -C build -j4
	$(Q)cd build/tests && make RunTests_coverage_html -j 5
clean:
	$(Q)rm -fr build
log:
	@echo "Showing logs from s2opcua_server plugin"
	$(Q)tail -f /var/log/syslog |grep -o 'Fledge s2opcua_.*$$'
	
check:
	@echo "Check validity of plugin 'libs2opcua.so'..."
	$(Q)! [ -z "$(FLEDGE_SRC)" ] || (echo "FLEDGE_SRC not set" && false)
	$(Q)$(FLEDGE_SRC)/cmake_build/C/plugins/utils/get_plugin_info build/libs2opcua.so plugin_info 2> /tmp/get_plugin_info.tmp
	$(Q)! ([ -s /tmp/get_plugin_info.tmp ] && cat /tmp/get_plugin_info.tmp)
        
install_plugin: check
	@echo "Install plugin..."
	$(Q)make -C build install
	@echo "Install demo certificates..."
	$(Q)mkdir -p $(FLEDGE_INSTALL)/data/etc/cert/ > /dev/null
	$(Q)cp -arf ./samples/cert/* $(FLEDGE_INSTALL)/data/etc/certs/s2opc_srv/
	
insert_task: del_plugin
	@echo "Insert plugin service in Fledge as task..."
	$(Q)! curl -sX POST http://localhost:8081/fledge/scheduled/task -d $(PLUGIN_TASK_CONF) \
	|  sed 's/^\(4.*\)/INSTALLATION FAILED : \1/; t; q 128 '
	@echo
	
insert_service: #del_plugin
	@echo "Insert plugin service in Fledge as service..."
	$(Q)! curl -sX POST http://localhost:8081/fledge/service -d $(PLUGIN_SERV_CONF) \
	|  sed 's/^\(4.*\)/INSTALLATION FAILED : \1/; t; q 128 '
	@echo

del_plugin:
	@echo "Delete plugin if already installed in FLEDGE..."
	$(Q)(curl -sX DELETE http://localhost:8081/fledge/scheduled/task/s2opcua_server | grep -o '"[ A-Za-z0-9_-]*deleted successfully."') || true
	$(Q)(curl -sX DELETE http://localhost:8081/fledge/service/s2opcua_service | grep -o '"[ A-Za-z0-9_-]*deleted successfully."') || true
	
cpplint:
	$(Q)cpplint --output=eclipse --repository=src --linelength=120 --filter=$(CPPLINT_EXCLUDE) --exclude=src/base_addrspace.c src/* include/*
		
.PHONY: all clean build check del_plugin install_plugin insert_service insert_task cpplint
