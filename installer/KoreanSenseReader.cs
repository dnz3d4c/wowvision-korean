using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;

namespace WowVisionInstaller
{
    /// <summary>
    /// Korean test builds only: lets WowVision speak through Sense Reader as well
    /// as NVDA. Runs right after <see cref="Sapi2SrInstaller"/>, whenever the NVDA
    /// bridge is installed.
    ///
    /// The SAPI2SR engine speaks through the nvdaControllerClient.dll next to it.
    /// This step moves that DLL (Sku's speech history proxy) to
    /// nvdaControllerClient.next.dll and puts the Sense Reader proxy
    /// (korean\sense-reader-bridge) in its place. The proxy hands every call to
    /// the next DLL first, so NVDA works as before; only when NVDA is not running
    /// and Sense Reader is, the text goes to Sense Reader.
    ///
    /// The shared bridge step copies Sku's proxy back on every run, so this step
    /// also runs on every run. WoW only loads signed engine DLLs, so the proxy is
    /// signed with the bridge's own signing script, extended at payload build time
    /// with the proxy's pin (payload\koSenseReader.zip, built by wowwork
    /// tools.installer_loc). A failure is said aloud and logged; NVDA keeps working.
    /// </summary>
    public static class KoreanSenseReader
    {
        public const string ProxyName = "nvdaControllerClient.dll";
        public const string NextName = "nvdaControllerClient.next.dll";
        public const string SignScriptName = "sign.ps1";
        private const string PayloadName = "koSenseReader.zip";
        private static readonly byte[] Marker = Encoding.ASCII.GetBytes("WowVisionKoreanSenseReaderProxy/");

        public static bool IsProxy(byte[] dll)
        {
            for (int i = 0; i + Marker.Length <= dll.Length; i++)
            {
                int j = 0;
                while (j < Marker.Length && dll[i + j] == Marker[j]) j++;
                if (j == Marker.Length) return true;
            }
            return false;
        }

        public static Stream OpenPayload()
        {
            var asm = Assembly.GetExecutingAssembly();
            string res = asm.GetManifestResourceNames()
                .FirstOrDefault(n => n.EndsWith(PayloadName, StringComparison.OrdinalIgnoreCase));
            if (res == null) throw new InvalidOperationException("Embedded Sense Reader payload not found.");
            return asm.GetManifestResourceStream(res);
        }

        /// <summary>Puts <paramref name="proxy"/> in the bridge folder. The DLL it
        /// replaces becomes the next one, unless that DLL is already the proxy (a
        /// second run). Returns false when the bridge is not installed.</summary>
        public static bool ApplyFiles(string bridgeDir, byte[] proxy)
        {
            if (!IsProxy(proxy)) throw new InvalidOperationException("The Sense Reader payload DLL is not the proxy.");
            string cur = Path.Combine(bridgeDir, ProxyName);
            string next = Path.Combine(bridgeDir, NextName);
            if (!File.Exists(cur)) return false;
            if (!IsProxy(File.ReadAllBytes(cur))) File.Copy(cur, next, true);
            else if (!File.Exists(next)) Logger.Warning($"Sense Reader proxy: {next} is missing; NVDA cannot be reached until the bridge is installed again.");
            File.WriteAllBytes(cur, proxy);
            return true;
        }

        public static void Install(Action<string> announce)
        {
            string staging = Path.Combine(Path.GetTempPath(), "WowVisionInstaller_kosensereader");
            try
            {
                if (Directory.Exists(staging)) Directory.Delete(staging, true);
                using (var payload = OpenPayload())
                using (var zip = new ZipArchive(payload, ZipArchiveMode.Read))
                    zip.ExtractToDirectory(staging);
                byte[] proxy = File.ReadAllBytes(Path.Combine(staging, ProxyName));
                if (!ApplyFiles(Sapi2SrInstaller.Dir64, proxy))
                {
                    Logger.Warning("Sense Reader proxy: the SAPI2SR bridge is not installed, nothing done.");
                    return;
                }
                Logger.Info($"Sense Reader proxy written into {Sapi2SrInstaller.Dir64}");
                Sapi2SrInstaller.RunSigning(Path.Combine(staging, SignScriptName), announce);
            }
            catch (Exception ex)
            {
                Logger.Error("Sense Reader proxy failed", ex);
                announce(Loc.Format("status.koreanSenseReaderFailed", ex.Message));
            }
            finally
            {
                try { if (Directory.Exists(staging)) Directory.Delete(staging, true); } catch { }
            }
        }
    }
}
