using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Linq;
using System.ServiceProcess;
using System.Text;

namespace MonitoringClientService
{
    public partial class MonitoringClientService : ServiceBase
    {
        Process client;

        public MonitoringClientService()
        {
            InitializeComponent();
        }

        protected override void OnStart(string[] args)
        {
            // Start Monitoring-Client
           client =  Process.Start("C:\\Tools\\Monitoring\\LMWinClient4.exe");
        }

        protected override void OnStop()
        {
            client.Kill();
        }
    }
}
