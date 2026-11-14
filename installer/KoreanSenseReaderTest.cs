using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;

namespace WowVisionInstaller
{
    /// <summary>Self-test "kosensereader" (WowVisionSelfTest only): the proxy
    /// swap in a throwaway SAPI2SR folder, and the payload the installer ships.</summary>
    internal static class KoreanSenseReaderTest
    {
        public static void Run(Action<string, bool> check)
        {
            Console.WriteLine("=== kosensereader: Sense Reader proxy in the SAPI2SR folder ===");
            byte[] Bytes(string s) => Encoding.ASCII.GetBytes(s);
            byte[] proxy = Bytes("MZ proxy WowVisionKoreanSenseReaderProxy/1 end");
            byte[] signedProxy = Bytes("MZ proxy WowVisionKoreanSenseReaderProxy/1 end + signature");
            byte[] sku = Bytes("MZ sku speech history proxy");
            byte[] sku2 = Bytes("MZ sku speech history proxy, newer");

            check("marker recognised", KoreanSenseReader.IsProxy(proxy) && KoreanSenseReader.IsProxy(signedProxy));
            check("Sku proxy is not ours", !KoreanSenseReader.IsProxy(sku));

            string root = Path.Combine(Path.GetTempPath(), "wv-kosensereader-" + Guid.NewGuid().ToString("N"));
            string cur = Path.Combine(root, KoreanSenseReader.ProxyName);
            string next = Path.Combine(root, KoreanSenseReader.NextName);
            try
            {
                check("no bridge installed: nothing done", !KoreanSenseReader.ApplyFiles(root, proxy) && !Directory.Exists(root));

                Directory.CreateDirectory(root);
                File.WriteAllBytes(cur, sku);
                check("applied over the bridge", KoreanSenseReader.ApplyFiles(root, proxy));
                check("the replaced DLL becomes the next one", File.ReadAllBytes(next).SequenceEqual(sku));
                check("the proxy is in place", File.ReadAllBytes(cur).SequenceEqual(proxy));

                // second run: the installed proxy (now signed) must not become its own next
                File.WriteAllBytes(cur, signedProxy);
                check("second run applies again", KoreanSenseReader.ApplyFiles(root, proxy));
                check("second run keeps the next DLL", File.ReadAllBytes(next).SequenceEqual(sku));
                check("second run writes the proxy", File.ReadAllBytes(cur).SequenceEqual(proxy));

                // the shared bridge step ran again and put a newer Sku proxy back
                File.WriteAllBytes(cur, sku2);
                KoreanSenseReader.ApplyFiles(root, proxy);
                check("a newer replaced DLL becomes the next one", File.ReadAllBytes(next).SequenceEqual(sku2));
                check("and the proxy goes back in place", File.ReadAllBytes(cur).SequenceEqual(proxy));

                bool threw = false;
                try { KoreanSenseReader.ApplyFiles(root, sku); }
                catch (InvalidOperationException) { threw = true; }
                check("a payload DLL without the marker is refused", threw && File.ReadAllBytes(cur).SequenceEqual(proxy));
            }
            finally { try { Directory.Delete(root, true); } catch { } }

            check("the failure line exists in every table",
                  new[] { Lang.En, Lang.De, Lang.Fr, Lang.Ko }.All(l => Loc.Table(l).ContainsKey("status.koreanSenseReaderFailed")));

            using (var payload = KoreanSenseReader.OpenPayload())
            using (var zip = new ZipArchive(payload, ZipArchiveMode.Read))
            {
                var dll = zip.GetEntry(KoreanSenseReader.ProxyName);
                var sign = zip.GetEntry(KoreanSenseReader.SignScriptName);
                check("payload has the proxy", dll != null);
                check("payload has the signing script", sign != null);
                if (dll != null)
                    using (var ms = new MemoryStream())
                    {
                        using (var s = dll.Open()) s.CopyTo(ms);
                        check("payload proxy carries the marker", KoreanSenseReader.IsProxy(ms.ToArray()));
                    }
                if (sign != null)
                    using (var r = new StreamReader(sign.Open()))
                    {
                        string text = r.ReadToEnd();
                        check("signing script pins the proxy", text.Contains("WowVision Korean Sense Reader proxy"));
                        check("signing script keeps the bridge pins", text.Contains("CCCD06D1CC0996735FD51ABD731932E10132B38ED3A7FF4F1CB9FB0666DE913F"));
                    }
            }
        }
    }
}
