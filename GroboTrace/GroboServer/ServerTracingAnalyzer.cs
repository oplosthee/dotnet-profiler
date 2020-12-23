using GroboTrace;
using GroboTrace.Core;
using System;
using System.Threading;

namespace GroboServer
{
    public static class ServerTracingAnalyzer
    {

        private static MethodCallTree[] CreateMethodCallTreesMap()
        {
            var result = new MethodCallTree[100000];
            for (var i = 0; i < result.Length; ++i)
                result[i] = new MethodCallTree();
            return result;
        }

        public static void MethodStarted(int methodId)
        {
            // TODO: Receive and parse data.
            GetMethodCallTreeForCurrentThread().StartMethod(methodId);
        }

        public static void MethodFinished(int methodId, long elapsed)
        {
            // TODO: Receive and parse data.
            GetMethodCallTreeForCurrentThread().FinishMethod(methodId, elapsed);

        }

        public static void ClearStats()
        {
            GetMethodCallTreeForCurrentThread().ClearStats();
        }

        public static Stats GetStats()
        {
            // TODO: Calls to MethodBaseTracingInstaller probably need a workaround.
            var ticks = MethodBaseTracingInstaller.TicksReader();
            var methodCallTree = GetMethodCallTreeForCurrentThread();
            return new Stats
            {
                ElapsedTicks = ticks - methodCallTree.startTicks,
                Tree = methodCallTree.GetStatsAsTree(ticks),
                List = methodCallTree.GetStatsAsList(ticks),
            };
        }

        private static MethodCallTree GetMethodCallTreeForCurrentThread()
        {
            var id = Thread.CurrentThread.ManagedThreadId;
            if (id >= callTreesMap.Length)
                throw new NotSupportedException($"Current ThreadId is too large: {id}");
            return callTreesMap[id];
        }

        private static readonly MethodCallTree[] callTreesMap = CreateMethodCallTreesMap();
    }
}