using GroboTrace.Core.NamedPipeWrapper;
using RGiesecke.DllExport;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Formatters.Binary;
using System.Threading;

namespace GroboTrace.Core
{
    public static class TracingAnalyzer
    {
        public static int AMSI_DEBUG_FUNCID = -1;

        //private static readonly NamedPipeClientStream client = new NamedPipeClientStream("TracerLogs");
        private static readonly NamedPipeClient<TracerPacket> client = new NamedPipeClient<TracerPacket>("TracerLogs");
        private static readonly IFormatter formatter = new BinaryFormatter();

        static TracingAnalyzer()
        {
            /*client.Error += OnError;
            client.Start();
            Trace.WriteLine("Started client - waiting for connection..");
            client.WaitForConnection();

            // We send the ID of the this (the program being traced) thread so that the server determine when it terminates.
            TracerPacket packet = new TracerPacket(TracerPacket.PacketType.ThreadIdentifier, Process.GetCurrentProcess().Id, null, -1);
            client.PushMessage(packet);*/
        }

        private static void OnError(Exception exception)
        {
            Trace.TraceError($"Error: {exception}");
        }

        private static MethodCallTree[] CreateMethodCallTreesMap()
        {
            var result = new MethodCallTree[100000];
            for (var i = 0; i < result.Length; ++i)
                result[i] = new MethodCallTree();
            return result;
        }

        public static void MethodStarted(int methodId)
        {
            try
            {
                MethodBase method = MethodBaseTracingInstaller.GetMethod(methodId);
                TracerPacket packet = new TracerPacket(TracerPacket.PacketType.MethodStarted, methodId, method, -1);
                client.PushMessage(packet);
            }
            catch (Exception ex)
            {
                Trace.WriteLine(ex.ToString());
            }

            // Delegated to the GroboServer:
            //GetMethodCallTreeForCurrentThread().StartMethod(methodId);
        }

        public static void MethodFinished(int methodId, long elapsed)
        {
            try
            {
                MethodBase method = MethodBaseTracingInstaller.GetMethod(methodId);
                TracerPacket packet = new TracerPacket(TracerPacket.PacketType.MethodFinished, methodId, method, elapsed);
                client.PushMessage(packet);
            }
            catch (Exception ex)
            {
                Trace.WriteLine(ex.ToString());
            }

            // Delegated to the GroboServer:
            //GetMethodCallTreeForCurrentThread().FinishMethod(methodId, elapsed);
        }

        public static void ClearStats()
        {
            GetMethodCallTreeForCurrentThread().ClearStats();
        }

        public static Stats GetStats()
        {
            var ticks = MethodBaseTracingInstaller.TicksReader();
            var methodCallTree = GetMethodCallTreeForCurrentThread();
            return new Stats
            {
                ElapsedTicks = ticks - methodCallTree.startTicks,
                Tree = methodCallTree.GetStatsAsTree(ticks),
                List = methodCallTree.GetStatsAsList(ticks),
            };
        }

        public static Stats GetStatsForThread(int managedThreadId)
        {
            var ticks = MethodBaseTracingInstaller.TicksReader();
            var methodCallTree = GetMethodCallTreeForThread(managedThreadId);
            return new Stats
            {
                ElapsedTicks = ticks - methodCallTree.startTicks,
                Tree = methodCallTree.GetStatsAsTree(ticks),
                List = methodCallTree.GetStatsAsList(ticks),
            };
        }

        [DllExport(CallingConvention = CallingConvention.Cdecl)]
        public static void SaveStats()
        {
            // TODO: Remove - unused.
            Trace.WriteLine("Called SaveStats() in TracingAnalyzer!");
        }

        public static MethodCallTree GetMethodCallTreeForCurrentThread()
        {
            var id = Thread.CurrentThread.ManagedThreadId;
            /*if (id >= callTreesMap.Length)
                throw new NotSupportedException($"Current ThreadId is too large: {id}");
            return callTreesMap[id];*/
            return GetMethodCallTreeForThread(id);
        }

        public static MethodCallTree GetMethodCallTreeForThread(int managedThreadId)
        {
            /*if (managedThreadId >= callTreesMap.Length)
                throw new NotSupportedException($"Current ThreadId is too large: {managedThreadId}");
            return callTreesMap[managedThreadId];*/
            if (!callTreesDict.ContainsKey(managedThreadId))
                callTreesDict[managedThreadId] = new MethodCallTree();

            return callTreesDict[managedThreadId];
        }

        private static readonly Dictionary<int, MethodCallTree> callTreesDict = new Dictionary<int, MethodCallTree>();
        //private static readonly MethodCallTree[] callTreesMap = CreateMethodCallTreesMap();
    }
}