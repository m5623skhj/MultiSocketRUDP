using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class PacketParserNode : ContextNodeBase
    {
        public string SetterMethodName { get; set; } = "";

        protected override void ExecuteImpl(RuntimeContext context)
        {
            if (string.IsNullOrEmpty(SetterMethodName))
            {
                Log.Warning("PacketParserNode: No setter method configured");
                return;
            }

            try
            {
                VariableAccessorRegistry.InvokeSetter(SetterMethodName, context, context.Packet);
                Log.Information($"PacketParserNode: Invoked setter '{SetterMethodName}'");
            }
            catch (Exception ex)
            {
                Log.Error($"PacketParserNode failed to invoke setter '{SetterMethodName}': {ex.Message}");
            }
        }
    }
}