using MultiSocketRUDPBotTester.Bot;
using System.Globalization;

namespace MultiSocketRUDPBotTester.Evaluation
{
    public static class ConditionEvaluator
    {
        public static bool Evaluate(
            RuntimeContext ctx,
            string? leftType,
            string left,
            string op,
            string? rightType,
            string right)
        {
            try
            {
                var leftValue = ResolveValue(ctx, leftType, left);
                var rightValue = ResolveValue(ctx, rightType, right);

                if (TryConvertToNumber(leftValue, out var lNum) &&
                    TryConvertToNumber(rightValue, out var rNum))
                {
                    return op switch
                    {
                        ">" => lNum > rNum,
                        "<" => lNum < rNum,
                        "==" => Math.Abs(lNum - rNum) < 0.0001,
                        ">=" => lNum >= rNum,
                        "<=" => lNum <= rNum,
                        "!=" => Math.Abs(lNum - rNum) >= 0.0001,
                        _ => false
                    };
                }

                return op switch
                {
                    "==" => Equals(leftValue, rightValue),
                    "!=" => !Equals(leftValue, rightValue),
                    _ => false
                };
            }
            catch (Exception ex)
            {
                Serilog.Log.Error("Condition evaluation failed: {Message}", ex.Message);
                return false;
            }
        }

        private static object ResolveValue(RuntimeContext ctx, string? type, string value)
        {
            if (type == "Getter Function")
            {
                return VariableAccessorRegistry.InvokeGetter(value, ctx) ?? 0;
            }

            return ParseConstant(value);
        }

        private static object ParseConstant(string value)
        {
            if (int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var intVal))
            {
                return intVal;
            }
            if (double.TryParse(value, NumberStyles.Float | NumberStyles.AllowThousands, CultureInfo.InvariantCulture, out var dblVal))
            {
                return dblVal;
            }
            if (bool.TryParse(value, out var boolVal))
            {
                return boolVal;
            }

            return value;
        }

        private static bool TryConvertToNumber(object value, out double result)
        {
            result = 0;
            switch (value)
            {
                case int i: result = i; return true;
                case double d: result = d; return true;
                case float f: result = f; return true;
                case long l: result = l; return true;
                case uint ui: result = ui; return true;
                case short s: result = s; return true;
                case byte b: result = b; return true;
                case string str when double.TryParse(str,
                    NumberStyles.Float | NumberStyles.AllowThousands,
                    CultureInfo.InvariantCulture,
                    out var parsed):
                    result = parsed;
                    return true;
                default:
                    return false;
            }
        }
    }
}