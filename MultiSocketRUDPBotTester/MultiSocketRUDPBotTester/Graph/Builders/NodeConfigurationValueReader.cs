using System.Globalization;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester.Graph.Builders
{
    internal static class NodeConfigurationValueReader
    {
        public static int GetInt(NodeVisual visual, string key, int defaultValue)
        {
            if (TryGetValue(visual, key, out var value) == false || value == null)
            {
                return defaultValue;
            }

            return value switch
            {
                int intValue => intValue,
                long longValue => checked((int)longValue),
                double doubleValue => checked((int)doubleValue),
                float floatValue => checked((int)floatValue),
                decimal decimalValue => checked((int)decimalValue),
                string stringValue when int.TryParse(stringValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed) => parsed,
                _ => defaultValue
            };
        }

        public static bool GetBool(NodeVisual visual, string key, bool defaultValue)
        {
            if (TryGetValue(visual, key, out var value) == false || value == null)
            {
                return defaultValue;
            }

            return value switch
            {
                bool boolValue => boolValue,
                string stringValue when bool.TryParse(stringValue, out var parsed) => parsed,
                int intValue => intValue != 0,
                long longValue => longValue != 0,
                _ => defaultValue
            };
        }

        private static bool TryGetValue(NodeVisual visual, string key, out object? value)
        {
            value = null;
            return visual.Configuration?.Properties.TryGetValue(key, out value) == true;
        }
    }
}
