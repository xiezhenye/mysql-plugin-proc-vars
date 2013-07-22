mysql-plugin-proc-vars
======================

mysql information plugin to show all variables of every process

## usage


first, compile the plugin and install in to plugin dir

    cp -r src /path/to/mysql-src/plugin/proc_vars
    cd /path/to/mysql-src
    cmake .
    cd plugin/proc_vars
    make
    make install
    
then, load the plugin into mysql

    mysql> INSTALL PLUGIN PROCESS_VARIABLES SONAME 'proc_vars.so';
    
thus, you can found `PROCESS_VARIABLES` table in `information_schema` database

try `select * from information_schema.PROCESS_VARIABLES where NAME = 'autocommit';` 
