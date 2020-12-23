using System;
using System.Reflection;
using System.Threading;

namespace GroboTrace.Core
{
    [Serializable]
    public class TracerPacket
    {
        [Serializable]
        public enum PacketType
        {
            ThreadIdentifier,
            MethodStarted,
            MethodFinished,
            MethodTailcall
        }

        public TracerPacket() { }

        public TracerPacket(PacketType type, int methodId, MethodBase methodBase, long elapsed)
        {
            this.Type = type;
            this.ManagedThreadId = Thread.CurrentThread.ManagedThreadId;
            this.ThreadName = Thread.CurrentThread.Name;
            this.MethodId = methodId;
            this.Method = methodBase;
            this.Elapsed = elapsed;
        }

        public PacketType Type
        { get; set; }

        public int ManagedThreadId
        { get; set; }

        public String ThreadName
        { get; set; }

        public int MethodId
        { get; set; }

        public MethodBase Method
        { get; set; }

        public long Elapsed
        { get; set; } = 0;
    }
}
