﻿//using System;
//using System.Collections;
//using System.Collections.Generic;
//
//using GroboTrace;
//
//using NUnit.Framework;
//
//namespace Tests
//{
//    public class TestInterface : TestBase
//    {
//        [Test]
//        public void TestImplIsCorrectlyCalled()
//        {
//            Type type;
//            tracingWrapper.TryWrap(typeof(I1), out type);
//            var i1 = (I1)Activator.CreateInstance(type, new object[] {new C1()});
//            int z;
//            Assert.AreEqual("3", i1.F(1, out z));
//            Assert.AreEqual(2, z);
//            i1.X = 5;
//            Assert.AreEqual(5, i1.X);
//            var i2 = (I2)i1;
//            i2.X = "zzz";
//            Assert.AreEqual("zzz", i2.X);
//        }
//
//        [Test]
//        public void TestReturnTypeIsInterface()
//        {
//            var i3 = Create<I3, C3>(() => new C3(tracingWrapper));
//            var i2 = i3.GetI2("zzz");
//            Assert.AreEqual("zzz", i2.X);
//        }
//
//        [Test]
//        public void TestIEnumerable()
//        {
//            Type type;
//            tracingWrapper.TryWrap(typeof(IEnumerable<int>), out type);
//            var enumerable = (IEnumerable<int>)Activator.CreateInstance(type, new List<int> {1, 2, 3});
//            foreach(var item in enumerable)
//                Console.WriteLine(item);
//            var enumerator = enumerable.GetEnumerator();
//            Console.WriteLine(enumerator.MoveNext());
//        }
//
//        [Test]
//        public void TestCycle()
//        {
//            TracingWrapper.DebugOutputDirectory = "c:\\temp";
//            Type type;
//            tracingWrapper.TryWrap(typeof(IZzz<int>), out type);
//            var zzz = (IZzz<int>)Activator.CreateInstance(type, new object[] {new Zzz<int>(new Qzz<int>())});
//            var qzz = zzz.GetQzz(0);
//            var zzz2 = qzz.GetZzz(0);
//            Console.WriteLine(zzz2.ToString());
//        }
//
//        [Test]
//        public void Test()
//        {
//            Type type;
//            tracingWrapper.TryWrap(typeof(IA), out type);
//            var a = (IA)Activator.CreateInstance(type, new object[] {new A()});
//            foreach(var x in a.Do())
//                Console.WriteLine(x);
//        }
//
//        public interface IA
//        {
//            IEnumerable<int> Do();
//        }
//
//        public class A : IA
//        {
//            public IEnumerable<int> Do()
//            {
//                yield return 1;
//            }
//        }
//
//        public interface IZzz<T>
//        {
//            IQzz<T> GetQzz(T t);
//            IEnumerable<int> Qxx();
//        }
//
//        public interface IQzz<T>
//        {
//            IZzz<T> GetZzz(T x);
//        }
//
//        public class Zzz<T> : IZzz<T>
//        {
//            private readonly Qzz<T> qzz;
//
//            public Zzz(Qzz<T> qzz)
//            {
//                this.qzz = qzz;
//            }
//
//            public IQzz<T> GetQzz(T t)
//            {
//                return qzz;
//            }
//
//            public IEnumerable<int> Qxx()
//            {
//                yield return 1;
//            }
//        }
//
//        public class Qzz<T> : IQzz<T>
//        {
//            public IZzz<T> GetZzz(T x)
//            {
//                return new Zzz<T>(this);
//            }
//        }
//
//        public class EnumerableWrapper<T> : IEnumerable<T>
//        {
//            public EnumerableWrapper(IEnumerable<T> enumerable)
//            {
//                this.enumerable = enumerable;
//            }
//
//            public IEnumerator<T> GetEnumerator()
//            {
//                return new EnumeratorWrapper<T>(enumerable);
//            }
//
//            IEnumerator IEnumerable.GetEnumerator()
//            {
//                return GetEnumerator();
//            }
//
//            private readonly IEnumerable<T> enumerable;
//        }
//
//        public class EnumeratorWrapper<T> : IEnumerator<T>
//        {
//            public EnumeratorWrapper(IEnumerable<T> enumerable)
//            {
//            }
//
//            public void Dispose()
//            {
//            }
//
//            public bool MoveNext()
//            {
//                return false;
//            }
//
//            public void Reset()
//            {
//            }
//
//            public T Current { get { return default(T); } }
//
//            object IEnumerator.Current { get { return Current; } }
//        }
//
//        public class C1 : I1
//        {
//            public string F(int x, out int z)
//            {
//                z = x + 1;
//                return (x + 2).ToString();
//            }
//
//            public void DoNothing()
//            {
//            }
//
//            public int X { get; set; }
//            string I2.X { get; set; }
//        }
//
//        public class C3 : I3
//        {
//            private readonly TracingWrapper tracingWrapper;
//
//            public C3(TracingWrapper tracingWrapper)
//            {
//                this.tracingWrapper = tracingWrapper;
//            }
//
//            public I2 GetI2(string x)
//            {
//                var result = Create<I1, C1>(tracingWrapper);
//                ((I2)result).X = x;
//                return result;
//            }
//        }
//
//        public interface I2
//        {
//            string X { get; set; }
//        }
//
//        public interface I1 : I2
//        {
//            string F(int x, out int z);
//            void DoNothing();
//            int X { get; set; }
//        }
//
//        public interface I3
//        {
//            I2 GetI2(string x);
//        }
//    }
//}