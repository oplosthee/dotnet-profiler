using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace GroboTrace.Core
{
    public class MethodCallTree
    {
        public MethodCallTree()
        {
            root = new MethodCallNode(null, 0);
            current = root;
            startTicks = MethodBaseTracingInstaller.TicksReader();
        }

        public void StartMethod(int methodId)
        {
            current = current.StartMethod(methodId);
        }

        public void FinishMethod(int methodId, long elsapsed)
        {
            current = current.FinishMethod(methodId, elsapsed);
        }

        public MethodStatsNode GetStatsAsTree(long endTicks)
        {
            var elapsedTicks = endTicks - startTicks;
            var result = current.GetStats(elapsedTicks);
            result.MethodStats.Percent = 100.0;
            return result;
        }

        public List<MethodStats> GetStatsAsList(long endTicks)
        {
            var elapsedTicks = endTicks - startTicks;
            var statsDict = new Dictionary<MethodBase, MethodStats>();
            foreach (var child in current.Children)
                child.GetStats(statsDict);
            var result = statsDict.Values.Concat(new[] { new MethodStats { Calls = 1, Ticks = elapsedTicks - statsDict.Values.Sum(node => node.Ticks) } }).OrderByDescending(stats => stats.Ticks).ToList();
            foreach (var stats in result)
                stats.Percent = stats.Ticks * 100.0 / elapsedTicks;
            return result;
        }

        public void ClearStats()
        {
            current.ClearStats();
            startTicks = MethodBaseTracingInstaller.TicksReader();
        }

        private readonly MethodCallNode root;
        private MethodCallNode current;
        public long startTicks;
    }
}