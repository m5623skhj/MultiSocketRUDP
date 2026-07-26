using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class ChoiceOption
    {
        public string Name { get; set; } = "";
        public int Weight { get; set; } = 1;
        public ActionNodeBase? Node { get; set; }
    }

    public class RandomChoiceNode : ActionNodeBase
    {
        public List<ChoiceOption> Choices { get; set; } = [];

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            client.GlobalContext.SetPacket(receivedPacket);
            ExecuteImpl(client.GlobalContext, maxValue => Random.Shared.NextInt64(0, maxValue));
        }

        internal void ExecuteImpl(RuntimeContext context, Func<long, long> nextRandom)
        {
            if (Choices.Count == 0)
            {
                Log.Warning("RandomChoiceNode: No choices defined");
                return;
            }

            if (Choices.Any(choice => choice.Weight <= 0))
            {
                Log.Warning("RandomChoiceNode: Choice weights must be positive");
                return;
            }

            var totalWeight = Choices.Sum(choice => (long)choice.Weight);
            var randomValue = nextRandom(totalWeight);
            if (randomValue < 0 || randomValue >= totalWeight)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(nextRandom),
                    $"Random value {randomValue} must be in [0, {totalWeight}).");
            }

            long cumulativeWeight = 0;
            ChoiceOption? selectedChoice = null;

            foreach (var choice in Choices)
            {
                cumulativeWeight += choice.Weight;
                if (randomValue >= cumulativeWeight)
                {
                    continue;
                }

                selectedChoice = choice;
                break;
            }

            if (selectedChoice?.Node != null)
            {
                Log.Information("RandomChoiceNode: Selected '{ChoiceName}' (weight: {Weight}/{TotalWeight})",
                    selectedChoice.Name, selectedChoice.Weight, totalWeight);
                var choiceVisited = new HashSet<ActionNodeBase>();
                NodeExecutionHelper.ExecuteChain(context, selectedChoice.Node, choiceVisited);
            }
            else
            {
                Log.Warning("RandomChoiceNode: No choice was selected");
            }

            var nextVisited = new HashSet<ActionNodeBase>();
            foreach (var nextNode in NextNodes)
            {
                NodeExecutionHelper.ExecuteChain(context, nextNode, nextVisited);
            }
        }
    }
}
