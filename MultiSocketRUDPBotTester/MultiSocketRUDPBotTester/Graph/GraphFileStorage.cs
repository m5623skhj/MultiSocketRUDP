using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace MultiSocketRUDPBotTester.Graph
{
    /// <summary>
    /// Persists graph-file models without coupling file I/O to the graph editor window.
    /// </summary>
    internal static class GraphFileStorage
    {
        public const string FileFilter = "Bot Graph (*.botgraph.json)|*.botgraph.json|JSON Files (*.json)|*.json";
        public const string DefaultExtension = ".botgraph.json";
        public const string DefaultFileName = "BotActionGraph.botgraph.json";

        private static readonly JsonSerializerOptions serializerOptions = new()
        {
            WriteIndented = true,
            Converters = { new JsonStringEnumConverter() }
        };

        public static void Save(string path, GraphFileModel graphFile)
        {
            var json = JsonSerializer.Serialize(graphFile, serializerOptions);
            File.WriteAllText(path, json);
        }

        public static GraphFileModel Load(string path)
        {
            var json = File.ReadAllText(path);
            var graphFile = JsonSerializer.Deserialize<GraphFileModel>(json, serializerOptions);
            if (graphFile == null || graphFile.Nodes.Count == 0)
            {
                throw new InvalidOperationException("Graph file is empty or invalid.");
            }

            return graphFile;
        }
    }
}
