# anyservice
将任意程序或脚本安装成Windows服务

Usage:
anyservice --remove  <ServcieName>
                     Remove service
anyservice --install <ServcieName> [optional] <Application> [Arguments]
                     Install service
    optional:
        --workdir     WorkDir      Working directory of application.
        --displayname DisplayName  Display name of the service.
        --description Description  Description of the service.
        --dependence  Dependence   Dependences of the service.
        --username    Username     Username of the service.
        --password    Password     Password of the service.
        --interactive              Install an interactive service.
        --stopchild                Stop subprocess when service stop.
        --norestart                Do not restart when process stop.
        --auto                     Auto start during system startup.
