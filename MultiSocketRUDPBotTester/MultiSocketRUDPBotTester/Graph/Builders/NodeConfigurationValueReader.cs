using System.Globalization;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester.Graph.Builders
{
    internal static class NodeConfigurationValueReader
    {
        /// <summary>
        /// 지정된 NodeVisual의 구성에서 지정된 키에 해당하는 정수 값을 안전하게 가져옵니다.
        /// 값이 없거나, null이거나, 정수로 변환할 수 없는 경우 기본값을 반환합니다.
        /// long, double, float, decimal, string (정수 파싱 가능) 유형을 int로 변환할 수 있습니다.
        /// 변환 시 오버플로우가 발생할 수 있는 경우 checked 컨텍스트에서 int로 캐스팅됩니다.
        /// 실패 조건: TryGetValue가 실패하거나, 값이 null이거나, 변환이 실패하면 defaultValue를 반환합니다.
        /// 상태 변화: 이 함수는 NodeVisual의 상태를 직접 변경하지 않습니다.
        /// Side Effect: 없습니다.
        /// </summary>
        /// <param name="visual">값을 읽어올 NodeVisual 객체입니다.</param>
        /// <param name="key">구성 속성에서 찾을 키입니다.</param>
        /// <param name="defaultValue">키를 찾을 수 없거나 값이 null이거나 변환할 수 없는 경우 반환할 기본 정수 값입니다.</param>
        /// <returns>변환된 정수 값 또는 기본값입니다.</returns>
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

        /// <summary>
        /// 지정된 NodeVisual의 구성에서 지정된 키에 해당하는 부울 값을 안전하게 가져옵니다.
        /// 값이 없거나, null이거나, 부울로 변환할 수 없는 경우 기본값을 반환합니다.
        /// bool, string (부울 파싱 가능), int, long 유형을 부울로 변환할 수 있습니다.
        /// int 또는 long의 경우 0이 아니면 true로 간주됩니다.
        /// 실패 조건: TryGetValue가 실패하거나, 값이 null이거나, 변환이 실패하면 defaultValue를 반환합니다.
        /// 상태 변화: 이 함수는 NodeVisual의 상태를 직접 변경하지 않습니다.
        /// Side Effect: 없습니다.
        /// </summary>
        /// <param name="visual">값을 읽어올 NodeVisual 객체입니다.</param>
        /// <param name="key">구성 속성에서 찾을 키입니다.</param>
        /// <param name="defaultValue">키를 찾을 수 없거나 값이 null이거나 변환할 수 없는 경우 반환할 기본 부울 값입니다.</param>
        /// <returns>변환된 부울 값 또는 기본값입니다.</returns>
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

        /// <summary>
        /// 지정된 NodeVisual의 구성 속성에서 키에 해당하는 값을 가져오려고 시도합니다.
        /// 실패 조건: visual.Configuration이 null이거나, Properties가 null이거나, 키가 발견되지 않으면 false를 반환합니다.
        /// 상태 변화: out 매개변수 'value'에 검색된 값을 설정합니다.
        /// Side Effect: 없습니다.
        /// </summary>
        /// <param name="visual">구성 속성을 포함하는 NodeVisual 객체입니다.</param>
        /// <param name="key">찾을 키입니다.</param>
        /// <param name="value">메서드가 반환될 때, 키를 찾으면 해당 키와 연결된 값이 포함됩니다. 찾지 못하면 null입니다.</param>
        /// <returns>키를 찾으면 true이고, 그렇지 않으면 false입니다.</returns>
        private static bool TryGetValue(NodeVisual visual, string key, out object? value)
        {
            value = null;
            return visual.Configuration?.Properties.TryGetValue(key, out value) == true;
        }
    }
}
