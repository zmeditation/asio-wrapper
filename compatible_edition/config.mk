
# If your compiler cannot find the boost's location independently, please specify them explicitly like this(please uncomment them first):
#boost_include_dir = -I/usr/local/include/
#boost_lib_dir = -L/usr/local/lib/

cflag = -Wall -fexceptions
ifeq (${MAKECMDGOALS}, debug)
	cflag += -g -DDEBUG
	dir = debug
else
	cflag += -O -DNDEBUG
	lflag = -s
	dir = release
endif
cflag += -pthread ${ext_cflag} ${boost_include_dir}
lflag += -pthread ${boost_lib_dir} -lboost_system -lboost_thread ${ext_libs}

target = ${dir}/${module}
sources = ${shell ls *.cpp}
objects = ${patsubst %.cpp,${dir}/%.o,${sources}}
deps = ${patsubst %.o,%.d,${objects}}
${shell mkdir -p ${dir}}

release debug : ${target}
-include ${deps}
${target} : ${objects}
	${CXX} -o ${target} ${objects} ${lflag}
${objects} : ${dir}/%.o : %.cpp
	${CXX} ${cflag} -E -MMD -MT '${subst .cpp,.o,${dir}/$<}' -MF ${subst .cpp,.d,${dir}/$<} $< 1>/dev/null
	${CXX} ${cflag} -c $< -o $@

.PHONY : clean
clean:
	-rm -rf debug release

