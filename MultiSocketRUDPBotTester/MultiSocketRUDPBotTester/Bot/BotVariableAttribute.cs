using MultiSocketRUDPBotTester.Buffer;
using System.Reflection;

namespace MultiSocketRUDPBotTester.Bot
{
    public enum VariableAccessType
    {
        Get,
        Set,
        GetAndSet
    }

    [AttributeUsage(AttributeTargets.Method)]
    public class BotVariableAttribute(string variableName, string description, VariableAccessType accessType)
        : Attribute
    {
        public string VariableName { get; } = variableName;
        public string Description { get; } = description;
        public VariableAccessType AccessType { get; } = accessType;
    }

    public static class BotVariables
    {
        /*===========================================
        // Getter
        ===========================================*/



        /*============================================
        // Setter
        ===========================================*/
    }

    public class VariableAccessor
    {
        public string VariableName { get; set; } = "";
        public string Description { get; set; } = "";
        public string DisplayName { get; set; } = "";
        public VariableAccessType AccessType { get; set; }
        public MethodInfo Method { get; set; } = null!;
        public Type ReturnType { get; set; } = null!;

        public object? InvokeGetter(RuntimeContext ctx)
        {
            return Method.Invoke(null, [ctx]);
        }

        public void InvokeSetter(RuntimeContext ctx, NetBuffer? buffer)
        {
            Method.Invoke(null, [ctx, buffer]);
        }
    }

    public static class VariableAccessorRegistry
    {
        private static readonly Dictionary<string, VariableAccessor> Getters = new();
        private static readonly Dictionary<string, VariableAccessor> Setters = new();
        private static bool _isInitialized;

        static VariableAccessorRegistry()
        {
            Initialize();
        }

        public static void Initialize()
        {
            if (_isInitialized) return;

            var methods = typeof(BotVariables).GetMethods(BindingFlags.Public | BindingFlags.Static);

            foreach (var method in methods)
            {
                var attr = method.GetCustomAttribute<BotVariableAttribute>();
                if (attr == null) continue;

                var accessor = new VariableAccessor
                {
                    VariableName = attr.VariableName,
                    Description = attr.Description,
                    DisplayName = $"{attr.VariableName} - {attr.Description}",
                    AccessType = attr.AccessType,
                    Method = method,
                    ReturnType = method.ReturnType
                };

                if (attr.AccessType == VariableAccessType.Get || attr.AccessType == VariableAccessType.GetAndSet)
                {
                    Getters[method.Name] = accessor;
                }

                if (attr.AccessType == VariableAccessType.Set || attr.AccessType == VariableAccessType.GetAndSet)
                {
                    Setters[method.Name] = accessor;
                }
            }

            _isInitialized = true;
            Serilog.Log.Information($"VariableAccessorRegistry initialized: {Getters.Count} getters, {Setters.Count} setters");
        }

        public static List<VariableAccessor> GetAllGetters()
        {
            return Getters.Values.ToList();
        }

        public static List<VariableAccessor> GetAllSetters()
        {
            return Setters.Values.ToList();
        }

        public static VariableAccessor? GetGetter(string methodName)
        {
            return Getters.GetValueOrDefault(methodName);
        }

        public static VariableAccessor? GetSetter(string methodName)
        {
            return Setters.GetValueOrDefault(methodName);
        }

        public static object? InvokeGetter(string methodName, RuntimeContext ctx)
        {
            if (Getters.TryGetValue(methodName, out var accessor))
            {
                return accessor.InvokeGetter(ctx);
            }

            throw new KeyNotFoundException($"Getter '{methodName}' not found");
        }

        public static void InvokeSetter(string methodName, RuntimeContext ctx, NetBuffer? buffer)
        {
            if (!Setters.TryGetValue(methodName, out var accessor))
            {
                throw new KeyNotFoundException($"Setter '{methodName}' not found");
            }

            accessor.InvokeSetter(ctx, buffer);
        }
    }
}
