using GroboTrace;
using GroboTrace.Core;
using GroboTrace.Core.NamedPipeWrapper;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Reflection;
using System.Text;
using TracingAnalyzer = GroboTrace.Core.TracingAnalyzer;

namespace GroboServer
{
    [DontTrace]
    class Program
    {
        private static readonly bool ENABLE_LOGGING = false;
        private static readonly int BUFFER_SIZE = 32 * 1024 * 2; // A WCHAR is 2 bytes

        private static readonly List<int> tracedProcesses = new List<int>();
        private static readonly HashSet<int> tracedManagedThreadIds = new HashSet<int>();
        private static readonly Dictionary<int, String> tracedThreadNames = new Dictionary<int, string>();
        private static readonly Stack<long> functionStartTimes = new Stack<long>();
        private static readonly Dictionary<int, long> threadFirstSeen = new Dictionary<int, long>();
        private static readonly Dictionary<int, long> threadLastSeen = new Dictionary<int, long>();
        private static readonly Dictionary<int, MethodBase> methodMap = new Dictionary<int, MethodBase>();

        private static Stopwatch stopwatch;
        private static bool hasTraced = false;

        public class CustomType : Type
        {
            public override Guid GUID => throw new NotImplementedException();

            public override Module Module => throw new NotImplementedException();

            public override Assembly Assembly => throw new NotImplementedException();

            public override string FullName => throw new NotImplementedException();

            public override string Namespace => throw new NotImplementedException();

            public override string AssemblyQualifiedName => throw new NotImplementedException();

            public override Type BaseType => throw new NotImplementedException();

            public override Type UnderlyingSystemType => throw new NotImplementedException();

            public override string Name
            { get; }

            public override ConstructorInfo[] GetConstructors(BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override object[] GetCustomAttributes(bool inherit)
            {
                throw new NotImplementedException();
            }

            public override object[] GetCustomAttributes(Type attributeType, bool inherit)
            {
                throw new NotImplementedException();
            }

            public override Type GetElementType()
            {
                throw new NotImplementedException();
            }

            public override EventInfo GetEvent(string name, BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override EventInfo[] GetEvents(BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override FieldInfo GetField(string name, BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override FieldInfo[] GetFields(BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override Type GetInterface(string name, bool ignoreCase)
            {
                throw new NotImplementedException();
            }

            public override Type[] GetInterfaces()
            {
                throw new NotImplementedException();
            }

            public override MemberInfo[] GetMembers(BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override MethodInfo[] GetMethods(BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override Type GetNestedType(string name, BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override Type[] GetNestedTypes(BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override PropertyInfo[] GetProperties(BindingFlags bindingAttr)
            {
                throw new NotImplementedException();
            }

            public override object InvokeMember(string name, BindingFlags invokeAttr, Binder binder, object target, object[] args, ParameterModifier[] modifiers, CultureInfo culture, string[] namedParameters)
            {
                throw new NotImplementedException();
            }

            public override bool IsDefined(Type attributeType, bool inherit)
            {
                throw new NotImplementedException();
            }

            protected override TypeAttributes GetAttributeFlagsImpl()
            {
                throw new NotImplementedException();
            }

            protected override ConstructorInfo GetConstructorImpl(BindingFlags bindingAttr, Binder binder, CallingConventions callConvention, Type[] types, ParameterModifier[] modifiers)
            {
                throw new NotImplementedException();
            }

            protected override MethodInfo GetMethodImpl(string name, BindingFlags bindingAttr, Binder binder, CallingConventions callConvention, Type[] types, ParameterModifier[] modifiers)
            {
                throw new NotImplementedException();
            }

            protected override PropertyInfo GetPropertyImpl(string name, BindingFlags bindingAttr, Binder binder, Type returnType, Type[] types, ParameterModifier[] modifiers)
            {
                throw new NotImplementedException();
            }

            protected override bool HasElementTypeImpl()
            {
                throw new NotImplementedException();
            }

            protected override bool IsArrayImpl()
            {
                throw new NotImplementedException();
            }

            protected override bool IsByRefImpl()
            {
                throw new NotImplementedException();
            }

            protected override bool IsCOMObjectImpl()
            {
                throw new NotImplementedException();
            }

            protected override bool IsPointerImpl()
            {
                throw new NotImplementedException();
            }

            protected override bool IsPrimitiveImpl()
            {
                throw new NotImplementedException();
            }
        }

        public class CustomMethodBase : MethodBase
        {
            public override RuntimeMethodHandle MethodHandle => throw new NotImplementedException();

            public override MethodAttributes Attributes => throw new NotImplementedException();

            public override MemberTypes MemberType => throw new NotImplementedException();

            public override string Name
            { get; }

            public override Type DeclaringType => throw new NotImplementedException();

            public override Type ReflectedType
            { get; }

            public override object[] GetCustomAttributes(bool inherit)
            {
                throw new NotImplementedException();
            }

            public override object[] GetCustomAttributes(Type attributeType, bool inherit)
            {
                throw new NotImplementedException();
            }

            public override MethodImplAttributes GetMethodImplementationFlags()
            {
                throw new NotImplementedException();
            }

            public override ParameterInfo[] GetParameters()
            {
                throw new NotImplementedException();
            }

            public override object Invoke(object obj, BindingFlags invokeAttr, Binder binder, object[] parameters, CultureInfo culture)
            {
                throw new NotImplementedException();
            }

            public override bool IsDefined(Type attributeType, bool inherit)
            {
                throw new NotImplementedException();
            }
        }

        static void Main()
        {
            // We want to set this map for the server only, so we can keep track of the traced methods in the server.
            // If we do not do this the server would not know which method belongs to which method ID.
            // TODO: Clean up this absolutely disgusting piece of code.
            if (MethodBaseTracingInstaller.methodMap == null)
            {
                MethodBaseTracingInstaller.methodMap = new Dictionary<int, MethodBase>();
            }

            var server = new NamedPipeServerStream("tracer", PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.None, BUFFER_SIZE, BUFFER_SIZE);
            Console.WriteLine("Waiting for client to connect ..");
            server.WaitForConnection();
            Console.WriteLine("Client connected - listening for packets!");
            var reader = new StreamReader(server, Encoding.Unicode);//, false, BUFFER_SIZE);
            stopwatch = Stopwatch.StartNew();

            bool isListening = true;
            while (isListening)
            {
                //var start = stopwatch.ElapsedTicks;
                var line = reader.ReadLine();
                //Console.WriteLine(line);
                //var stop = stopwatch.ElapsedTicks;
                //Console.WriteLine($"{stop - start} -> {line}");
                if (line.Length > 0)
                {
                    //var processingStart = stopwatch.ElapsedTicks;
                    //Console.WriteLine(line);
                    Log($"Received: {line}");
                    var parts = line.Split('\x01');
                    if (parts.Length < 3 || parts.Length > 6 || !parts[0].EndsWith("PROFILER"))
                    {
                        Console.WriteLine($"Malformed message received: {line}");
                        Console.WriteLine("Stopping server and saving logs..");
                        break;
                    }

                    TracerPacket packet = new TracerPacket
                    {
                        ManagedThreadId = int.Parse(parts[1]),
                        ThreadName = parts[1] // We do not have thread names in unmanaged code, so set it to the ID for compatability.
                    };

                    switch (parts[2])
                    {
                        case "MAP":
                            MethodBase method = new CustomMethodBase();
                            Type type = new CustomType();

                            FieldInfo methodBaseNameField = typeof(CustomMethodBase).GetField("<Name>k__BackingField", BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static);
                            FieldInfo methodBaseTypeField = typeof(CustomMethodBase).GetField("<ReflectedType>k__BackingField", BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static);
                            FieldInfo typeNameField = typeof(CustomType).GetField("<Name>k__BackingField", BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static);

                            methodBaseNameField.SetValue(method, parts[5]);
                            methodBaseTypeField.SetValue(method, type);
                            typeNameField.SetValue(method.ReflectedType, parts[4]);

                            // TODO: It might be possible to merge this map with MethodBaseTracingInstaller.methodMap.
                            packet.MethodId = Math.Abs(int.Parse(parts[3]));
                            methodMap[packet.MethodId] = method;
                            continue;
                        case "ENTER":
                            packet.MethodId = Math.Abs(int.Parse(parts[3]));
                            packet.Type = TracerPacket.PacketType.MethodStarted;
                            break;
                        case "LEAVE":
                        case "TAILCALL":
                            // Both of these need to exit the current node in the call tree, so they're treated equally.
                            packet.MethodId = Math.Abs(int.Parse(parts[3]));
                            packet.Type = TracerPacket.PacketType.MethodFinished;
                            break;
                        case "QUIT":
                            isListening = false;
                            continue;
                        default:
                            Console.WriteLine($"Error: Unimplemented packet type: {parts[2]}");
                            return;
                    }

                    // These statements keep track of the lifetime of a thread.
                    // This is done so we can accurately calculate the time spent per function relative to the lifetime of the thread.
                    if (!threadFirstSeen.ContainsKey(packet.ManagedThreadId))
                    {
                        threadFirstSeen[packet.ManagedThreadId] = GetCurrentRuntime();
                    }

                    threadLastSeen[packet.ManagedThreadId] = GetCurrentRuntime();

                    // Sanity check to guarantee all called functions have been mapped. This should *NOT* happen.
                    if (!methodMap.ContainsKey(packet.MethodId))
                    {
                        Console.WriteLine($"Error: Called unmapped function: {line}");
                        return;
                    }
                    else
                    {
                        packet.Method = methodMap[packet.MethodId];
                    }

                    //Console.WriteLine($"Processing: {stopwatch.ElapsedTicks - processingStart}");
                    //var submitStart = stopwatch.ElapsedTicks;
                    OnClientMessage(null, packet);
                    //Console.WriteLine($"Submitting: {stopwatch.ElapsedTicks - submitStart}");
                }
            }

            stopwatch.Stop();
            server.Close();
            SaveLogs();
            while (true) ;
        }

        static void OldMain()
        {
            var server = new NamedPipeServer<TracerPacket>("TracerLogs");
            Console.WriteLine("Waiting for a message - Press Q to quit.");

            // We want to set this map for the server only, so we can keep track of the traced methods in the server.
            // If we do not do this the server would not know which method belongs to which method ID.
            // TODO: Clean up this absolutely disgusting piece of code.
            if (MethodBaseTracingInstaller.methodMap == null)
            {
                MethodBaseTracingInstaller.methodMap = new Dictionary<int, MethodBase>();
            }

            server.ClientConnected += OnClientConnected;
            server.ClientMessage += OnClientMessage;
            server.Error += OnError;
            server.Start();

            // Loop to check whether the processes that are being traced are still running.
            // In case no process has been traced yet, also run this loop even though the list will be empty.
            // TODO: Perhaps only the main thread should be included (in case background services are launched).
            while (!hasTraced || tracedProcesses.Count > 0)
            {
                // We iterate through the list backwards so we can update it while iterating.
                for (int i = tracedProcesses.Count - 1; i >= 0; i--)
                {
                    int id = tracedProcesses[i];

                    try
                    {
                        // This method throws an exception when the process does not exist.
                        Process.GetProcessById(id);
                    }
                    catch
                    {
                        Console.WriteLine($"Process ID {id} terminated.");
                        tracedProcesses.Remove(tracedProcesses[i]);
                    }
                }
            }

            stopwatch.Stop();
            server.Stop();
            #region Old code using the default NamedPipeServerStream
            /*using (var server = new NamedPipeServerStream("TracerLogs", PipeDirection.InOut, 1, PipeTransmissionMode.Message))
            {
                try
                {
                    IFormatter formatter = new BinaryFormatter();

                    Console.WriteLine("Waiting for a connection ..");
                    server.WaitForConnection();

                    while (true)
                    {
                        do
                        {
                            ClientPacket packet = (ClientPacket)formatter.Deserialize(server);
                            Console.WriteLine(packet.Type);
                        }
                        while (!server.IsMessageComplete);
                        
                        /*using (var reader = new StreamReader(server))
                        {
                            while (true)
                            {
                                
                                ClientPacket packet = (ClientPacket) formatter.Deserialize(server);
                                Console.WriteLine(packet.Type);
                                //var data = reader.ReadLine();
                                //if (data != null)
                                //    Console.WriteLine(data);
                            }

                        }*//*
                        //IFormatter formatter = new BinaryFormatter();
                        //Foo foo = (Foo)formatter.Deserialize(server);
                        //Console.WriteLine(foo.Bar);
                        //Console.WriteLine(foo.Baz);
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine(ex.ToString());
                }

            }*/
            #endregion

            Console.WriteLine("All processes have been terminated - saving logs ..");
            Console.WriteLine("---------------------------------------------");
            SaveLogs();
        }

        private static void SaveLogs()
        {
            using (System.IO.StreamWriter file = new System.IO.StreamWriter(@"C:\Users\IEUser\Desktop\log.txt"))
            {
                foreach (int managedThreadId in tracedManagedThreadIds)
                {
                    Console.WriteLine($"Printing logs for managed thread ID {managedThreadId}");
                    file.WriteLine($"Printing logs for managed thread {tracedThreadNames[managedThreadId]} (ID {managedThreadId})");
                    var stats = TracingAnalyzer.GetStatsForThread(managedThreadId);
                    var trace = TracingAnalyzerStatsFormatter.Format(stats, threadLastSeen[managedThreadId] - threadFirstSeen[managedThreadId]);

                    file.WriteLine(trace);
                    //Console.WriteLine(trace);

                    Console.WriteLine("---------------------------------------------");
                    file.WriteLine("---------------------------------------------");
                }
            }

            Console.WriteLine("Finished saving logs");
        }

        private static void OnClientMessage(NamedPipeConnection<TracerPacket, TracerPacket> connection, TracerPacket message)
        {
            var method = message.Method;
            tracedManagedThreadIds.Add(message.ManagedThreadId);
            tracedThreadNames[message.ManagedThreadId] = message.ThreadName;

            switch (message.Type)
            {
                case TracerPacket.PacketType.ThreadIdentifier:
                    // Skip the server from being included in the logs.
                    if (message.MethodId == Process.GetCurrentProcess().Id) break;

                    Console.WriteLine($"Process ID of traced application is {message.MethodId}");
                    tracedProcesses.Add(message.MethodId);
                    hasTraced = true;
                    break;
                case TracerPacket.PacketType.MethodStarted:
                    Log($"Started method {method.ReflectedType.Name}.{method.Name}");
                    MethodBaseTracingInstaller.AddMethodToMap(method, message.MethodId);
                    functionStartTimes.Push(GetCurrentRuntime());
                    TracingAnalyzer.GetMethodCallTreeForThread(message.ManagedThreadId).StartMethod(message.MethodId);
                    break;
                case TracerPacket.PacketType.MethodFinished:
                    Log($"Finished method {method.ReflectedType.Name}.{method.Name} in {message.Elapsed}");
                    TracingAnalyzer.GetMethodCallTreeForThread(message.ManagedThreadId).FinishMethod(message.MethodId, functionStartTimes.Pop());
                    break;
            }
        }

        private static void OnClientConnected(NamedPipeConnection<TracerPacket, TracerPacket> connection)
        {
            Console.WriteLine($"Client {connection.Id} is now connected.");
        }

        private static void OnError(Exception exception)
        {
            Console.Error.WriteLine($"Error: {exception}");
        }

        private static void Log(String message)
        {
            if (ENABLE_LOGGING)
            {
                Console.WriteLine(message);
            }
        }

        /// <summary>
        /// Helper function to get the current runtime in milliseconds from the stopwatch.
        /// </summary>
        /// <returns>The current runtime of the tracer in milliseconds</returns>
        private static long GetCurrentRuntime()
        {
            return stopwatch.ElapsedMilliseconds;
        }
    }
}
