using GroboLib;
using System;
using System.Collections.Generic;
using System.Linq;
using System.ServiceModel;
using System.ServiceModel.Description;
using System.Text;
using System.Threading.Tasks;

namespace GroboHost
{
    class Program
    {
        // Code copied from https://docs.microsoft.com/en-us/dotnet/framework/wcf/how-to-host-and-run-a-basic-wcf-service
        static void Main(string[] args)
        {
            Uri baseAddress = new Uri("http://localhost:8000/Grobo/");
            ServiceHost selfHost = new ServiceHost(typeof(Service), baseAddress);

            try
            {
                // Add the endpoint and enable metadata exchange.
                selfHost.AddServiceEndpoint(typeof(IService), new WSHttpBinding(), "Service");
                ServiceMetadataBehavior smb = new ServiceMetadataBehavior();
                smb.HttpGetEnabled = true;
                selfHost.Description.Behaviors.Add(smb);

                // Start the service.
                selfHost.Open();
                Console.WriteLine("Service has started - Press <Enter> to terminate the service.");
                Console.WriteLine();
                Console.ReadLine();
                selfHost.Close();
            }
            catch (CommunicationException ex)
            {
                Console.WriteLine($"An exception occured: {ex.Message}");
                selfHost.Abort();
            }
        }
    }
}
