// Generated homogeneous native views; source codec order remains unchanged.
namespace
{
    struct BatchControls
    {
        std::vector<TaurenAppearance::Option> options;
        std::vector<char const*> labels;
        std::vector<TaurenAppearance::Requirement> requirements;
    };

    template<class Options, class Labels, class Requirements>
    BatchControls MakeBatchControls(Options const& options, Labels const& labels, Requirements const& requirements)
    {
        BatchControls result;
        for (auto const& option : options)
            result.options.push_back({option.field, option.count, option.factor});
        for (auto const& label : labels)
            result.labels.push_back(label);
        for (auto const& requirement : requirements)
            result.requirements.push_back({requirement.option, requirement.value, requirement.required, requirement.mask});
        return result;
    }

    BatchControls const& BatchControlProfile(unsigned race, unsigned gender)
    {
        static BatchControls const empty;
        if (gender > 1)
            return empty;
        switch (race)
        {
            case 5:
            {
                static auto const male = MakeBatchControls(UndeadBatchAppearance::MaleOptions,
                    UndeadBatchAppearance::MaleLabels, UndeadBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(UndeadBatchAppearance::FemaleOptions,
                    UndeadBatchAppearance::FemaleLabels, UndeadBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 8:
            {
                static auto const male = MakeBatchControls(TrollBatchAppearance::MaleOptions,
                    TrollBatchAppearance::MaleLabels, TrollBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(TrollBatchAppearance::FemaleOptions,
                    TrollBatchAppearance::FemaleLabels, TrollBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 9:
            {
                static auto const male = MakeBatchControls(GoblinBatchAppearance::MaleOptions,
                    GoblinBatchAppearance::MaleLabels, GoblinBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(GoblinBatchAppearance::FemaleOptions,
                    GoblinBatchAppearance::FemaleLabels, GoblinBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 19:
            case 20:
            {
                static auto const male = MakeBatchControls(PandarenBatchAppearance::MaleOptions,
                    PandarenBatchAppearance::MaleLabels, PandarenBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(PandarenBatchAppearance::FemaleOptions,
                    PandarenBatchAppearance::FemaleLabels, PandarenBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 18:
            {
                static auto const male = MakeBatchControls(VulperaBatchAppearance::MaleOptions,
                    VulperaBatchAppearance::MaleLabels, VulperaBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(VulperaBatchAppearance::FemaleOptions,
                    VulperaBatchAppearance::FemaleLabels, VulperaBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 22:
            {
                static auto const male = MakeBatchControls(ZandalariBatchAppearance::MaleOptions,
                    ZandalariBatchAppearance::MaleLabels, ZandalariBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(ZandalariBatchAppearance::FemaleOptions,
                    ZandalariBatchAppearance::FemaleLabels, ZandalariBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 26:
            {
                static auto const male = MakeBatchControls(NightborneBatchAppearance::MaleOptions,
                    NightborneBatchAppearance::MaleLabels, NightborneBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(NightborneBatchAppearance::FemaleOptions,
                    NightborneBatchAppearance::FemaleLabels, NightborneBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 28:
            {
                static auto const male = MakeBatchControls(ManariBatchAppearance::MaleOptions,
                    ManariBatchAppearance::MaleLabels, ManariBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(ManariBatchAppearance::FemaleOptions,
                    ManariBatchAppearance::FemaleLabels, ManariBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 29:
            case 31:
            {
                static auto const male = MakeBatchControls(EarthenBatchAppearance::MaleOptions,
                    EarthenBatchAppearance::MaleLabels, EarthenBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(EarthenBatchAppearance::FemaleOptions,
                    EarthenBatchAppearance::FemaleLabels, EarthenBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 1:
            {
                static auto const male = MakeBatchControls(HumanBatchAppearance::MaleOptions,
                    HumanBatchAppearance::MaleLabels, HumanBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(HumanBatchAppearance::FemaleOptions,
                    HumanBatchAppearance::FemaleLabels, HumanBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 3:
            {
                static auto const male = MakeBatchControls(DwarfBatchAppearance::MaleOptions,
                    DwarfBatchAppearance::MaleLabels, DwarfBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(DwarfBatchAppearance::FemaleOptions,
                    DwarfBatchAppearance::FemaleLabels, DwarfBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 7:
            {
                static auto const male = MakeBatchControls(GnomeBatchAppearance::MaleOptions,
                    GnomeBatchAppearance::MaleLabels, GnomeBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(GnomeBatchAppearance::FemaleOptions,
                    GnomeBatchAppearance::FemaleLabels, GnomeBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 11:
            {
                static auto const male = MakeBatchControls(DraeneiBatchAppearance::MaleOptions,
                    DraeneiBatchAppearance::MaleLabels, DraeneiBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(DraeneiBatchAppearance::FemaleOptions,
                    DraeneiBatchAppearance::FemaleLabels, DraeneiBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 12:
            {
                static auto const male = MakeBatchControls(WorgenBatchAppearance::MaleOptions,
                    WorgenBatchAppearance::MaleLabels, WorgenBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(WorgenBatchAppearance::FemaleOptions,
                    WorgenBatchAppearance::FemaleLabels, WorgenBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 13:
            {
                static auto const male = MakeBatchControls(VoidelfBatchAppearance::MaleOptions,
                    VoidelfBatchAppearance::MaleLabels, VoidelfBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(VoidelfBatchAppearance::FemaleOptions,
                    VoidelfBatchAppearance::FemaleLabels, VoidelfBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 16:
            {
                static auto const male = MakeBatchControls(DarkironBatchAppearance::MaleOptions,
                    DarkironBatchAppearance::MaleLabels, DarkironBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(DarkironBatchAppearance::FemaleOptions,
                    DarkironBatchAppearance::FemaleLabels, DarkironBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 25:
            {
                static auto const male = MakeBatchControls(KultiranBatchAppearance::MaleOptions,
                    KultiranBatchAppearance::MaleLabels, KultiranBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(KultiranBatchAppearance::FemaleOptions,
                    KultiranBatchAppearance::FemaleLabels, KultiranBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            case 27:
            {
                static auto const male = MakeBatchControls(LightforgedBatchAppearance::MaleOptions,
                    LightforgedBatchAppearance::MaleLabels, LightforgedBatchAppearance::MaleRequirements);
                static auto const female = MakeBatchControls(LightforgedBatchAppearance::FemaleOptions,
                    LightforgedBatchAppearance::FemaleLabels, LightforgedBatchAppearance::FemaleRequirements);
                return gender ? female : male;
            }
            default: return empty;
        }
    }
}
