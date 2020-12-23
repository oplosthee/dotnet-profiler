using RGiesecke.DllExport;
using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace GroboTrace.Core
{
    public static class Loader
    {
        private static Assembly CurrentDomain_AssemblyResolve(object sender, ResolveEventArgs args)
        {
            Debug.WriteLine(args.Name);
            foreach (var dll in dlls)
            {
                if (args.Name.StartsWith(dll.FullName))
                {
                    var dllFileName = dll.FileName;
                    Debug.WriteLine($"Asked to load {dll}: {args.Name}");
                    return Assembly.LoadFrom(Path.Combine(profilerDirectory, dllFileName));
                }
            }
            return null;
        }

        [DllExport(CallingConvention = CallingConvention.Cdecl)]
        public static void SetProfilerPath([MarshalAs(UnmanagedType.LPWStr)] string profilerDirectory)
        {
            Trace.WriteLine("Profiler directory: " + profilerDirectory);

            Loader.profilerDirectory = profilerDirectory;

            AppDomain.CurrentDomain.AssemblyResolve += CurrentDomain_AssemblyResolve;
        }

        private static readonly DllName[] dlls =
            {
                new DllName("GroboTrace,", "GroboTrace.dll"),
                //new DllName("NamedPipeWrapper", "NamedPipeWrapper.dll"),
            };

        private static string profilerDirectory;

        private class DllName
        {
            public DllName(string fullName, string fileName)
            {
                FullName = fullName;
                FileName = fileName;
            }

            public string FullName { get; private set; }
            public string FileName { get; private set; }
        }
    }
}